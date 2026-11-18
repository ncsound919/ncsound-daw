#include "MixologyBridge.h"

#include "AudioEngine.h"
#include "MixRack.h"
#include "Session.h"

#include "Engine/ChainResolver.h"
#include "Engine/OverlaySessionWriter.h"

#include <cmath>
#include <string>
#include <vector>

#if JUCE_WINDOWS
    #include <windows.h>
    #include <winhttp.h>
    #if JUCE_MSVC
        #pragma comment(lib, "winhttp.lib")
    #endif
#endif

namespace nc
{
namespace
{
using mixology::BusTarget;

#if JUCE_WINDOWS
/** Blocking JSON POST. The DAW's autopilot runs on the explicit user action,
    so a short blocking call is preferable to another background thread. */
juce::String postJsonToMixology (const juce::String& endpoint,
                                 const juce::String& payload,
                                 juce::String& response,
                                 int& status)
{
    const juce::URL url (endpoint);
    const auto host = url.getDomain();

    if (host.isEmpty())
        return "the Mixology server address has no host";

    const auto secure = url.getScheme().equalsIgnoreCase ("https");
    const auto port = (INTERNET_PORT) (url.getPort() > 0 ? url.getPort() : (secure ? 443 : 80));
    auto path = "/" + url.getSubPath();

    if (url.getQueryString().isNotEmpty())
        path << url.getQueryString();

    auto open = [] (LPCWSTR agent)
    {
        return WinHttpOpen (agent, WINHTTP_ACCESS_TYPE_NO_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    };

    HINTERNET session = open (L"NC Mixology Autopilot/1.0");

    if (session == nullptr)
        return "WinHTTP could not open a session (" + juce::String ((int) GetLastError()) + ")";

    struct Closer
    {
        HINTERNET handle;
        ~Closer() { if (handle != nullptr) WinHttpCloseHandle (handle); }
    } sessionCloser { session };

    WinHttpSetTimeouts (session, 15000, 15000, 180000, 180000);

    HINTERNET connection = WinHttpConnect (session, host.toWideCharPointer(), port, 0);

    if (connection == nullptr)
        return "WinHTTP could not reach " + host;

    Closer connectionCloser { connection };

    HINTERNET request = WinHttpOpenRequest (connection, L"POST", path.toWideCharPointer(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           secure ? WINHTTP_FLAG_SECURE : 0);

    if (request == nullptr)
        return "WinHTTP could not create the request";

    Closer requestCloser { request };

    static constexpr wchar_t headers[] = L"Content-Type: application/json\r\nAccept: application/json\r\n";
    const auto bodySize = (DWORD) payload.getNumBytesAsUTF8();

    if (! WinHttpSendRequest (request, headers, (DWORD) -1,
                              (LPVOID) payload.toRawUTF8(), bodySize, bodySize, 0))
        return "WinHTTP could not send the request";

    if (! WinHttpReceiveResponse (request, nullptr))
        return "WinHTTP did not receive a response";

    DWORD httpStatus = 0;
    DWORD statusSize = sizeof (httpStatus);

    if (! WinHttpQueryHeaders (request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &httpStatus, &statusSize,
                               WINHTTP_NO_HEADER_INDEX))
        return "WinHTTP could not read the status";

    status = (int) httpStatus;
    response.clear();

    for (;;)
    {
        DWORD available = 0;

        if (! WinHttpQueryDataAvailable (request, &available))
            return "WinHTTP could not read the response";

        if (available == 0)
            break;

        std::vector<char> buffer (available);

        DWORD received = 0;
        if (! WinHttpReadData (request, buffer.data(), available, &received))
            return "WinHTTP could not read the response";

        response.append (buffer.data(), (int) received);
    }

    return {};
}
#endif

int findTrack (const juce::String& a, const juce::String& b, int fallback)
{
    const auto& tracks = sessionTracks();

    for (int i = 0; i < static_cast<int> (tracks.size()); ++i)
    {
        const auto name = tracks[static_cast<size_t> (i)].name.toUpperCase();

        if (name.contains (a) || (b.isNotEmpty() && name.contains (b)))
            return i;
    }

    return fallback;
}

int trackForBus (BusTarget bus, int activeTracks)
{
    int track = -1;

    switch (bus)
    {
        case BusTarget::lead_vocal: track = findTrack ("VOX", "VOCAL", 3); break;
        case BusTarget::bgv:        track = findTrack ("BGV", "BG VOX", 12); break;
        case BusTarget::adlib:      track = findTrack ("ADLIB", "AD LIB", 11); break;
        case BusTarget::bass808:    track = findTrack ("BASS", "808", 1); break;
        case BusTarget::drum_bus:   track = findTrack ("DRUM", "KIT", 0); break;
        case BusTarget::master:     track = -1; break;
    }

    return track >= 0 && track < activeTracks ? track : -1;
}

bool isVocalBus (BusTarget bus)
{
    return bus == BusTarget::lead_vocal || bus == BusTarget::bgv || bus == BusTarget::adlib;
}

BusTarget busFromName (const juce::String& name)
{
    if (name.equalsIgnoreCase ("lead_vocal")) return BusTarget::lead_vocal;
    if (name.equalsIgnoreCase ("bgv")) return BusTarget::bgv;
    if (name.equalsIgnoreCase ("adlib")) return BusTarget::adlib;
    if (name.equalsIgnoreCase ("808_bass") || name.equalsIgnoreCase ("bass808"))
        return BusTarget::bass808;
    if (name.equalsIgnoreCase ("drum_bus")) return BusTarget::drum_bus;
    return BusTarget::master;
}

//==============================================================================
// Native Earshot bridge. The DAW does not need the Node server to run an
// autonomous pass: it renders (or takes) a WAV, asks Earshot for its
// model-free measurement, and applies the same bounded plan shape the
// Mixology server returns.
//==============================================================================
juce::StringArray earshotHomeCandidates()
{
    juce::StringArray candidates;

    const auto env = juce::SystemStats::getEnvironmentVariable ("EARSHOT_HOME", {}).trim();
    if (env.isNotEmpty())
        candidates.add (env);

    // A packaged/self-installed copy lives under the per-user app data folder.
    candidates.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("NCSound")
                        .getChildFile ("earshot")
                        .getFullPathName());

    // Development fallbacks; harmless when absent.
    candidates.add ("D:/assets/earshot-main");
    candidates.add ("C:/assets/earshot-main");
    candidates.removeDuplicates (true);
    return candidates;
}

juce::String earshotHome()
{
    const auto candidates = earshotHomeCandidates();

    for (const auto& candidate : candidates)
        if (juce::File (candidate).isDirectory())
            return candidate;

    return candidates[0];
}

juce::String earshotPython()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("EARSHOT_PYTHON", {}).trim();
    if (env.isNotEmpty())
        return env;

    const auto candidates = earshotHomeCandidates();

    for (const auto& candidate : candidates)
    {
        const auto python = juce::File (candidate).getChildFile (".venv/Scripts/python.exe");

        if (python.existsAsFile())
            return python.getFullPathName();
    }

    return juce::File (earshotHome()).getChildFile (".venv/Scripts/python.exe").getFullPathName();
}

bool measureWithEarshot (const juce::File& file, juce::var& measure, juce::String& error)
{
    const auto home = earshotHome();
    const auto python = earshotPython();

    if (! juce::File (python).existsAsFile())
    {
        error = "EARSHOT_PYTHON not found: " + python;
        return false;
    }

    juce::String script;
    script << "import sys; sys.path.insert(0, r'" << home.replace ("'", "\\'") << "'); "
           << "from earshot.cli import main; "
           << "main(['measure', r'" << file.getFullPathName().replace ("'", "\\'")
           << "', '--music', '--json'])";

    juce::StringArray command;
    command.add (python);
    command.add ("-c");
    command.add (script);

    juce::ChildProcess process;

    if (! process.start (command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        error = "Could not start Earshot (" + python + ").";
        return false;
    }

    // A wedged Python process must not hang the caller (which is a worker
    // thread); give it two minutes, then kill it and report.
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 120000.0;

    while (process.isRunning())
    {
        if (juce::Time::getMillisecondCounterHiRes() > deadline)
        {
            process.kill();
            error = "Earshot did not finish within 120 seconds.";
            return false;
        }

        juce::Thread::sleep (25);
    }

    const auto output = process.readAllProcessOutput();
    const auto exitCode = process.getExitCode();

    const auto first = output.indexOfChar ('{');
    const auto last = output.lastIndexOfChar ('}');

    if (exitCode != 0 || first < 0 || last <= first)
    {
        error = "Earshot measure failed"
                + (exitCode != 0 ? " (exit " + juce::String (exitCode) + ")" : juce::String())
                + ": " + output.substring (0, 600).trim();
        return false;
    }

    const auto parsed = juce::JSON::parse (output.substring (first, last + 1));

    if (! parsed.isObject())
    {
        error = "Earshot returned a non-object measurement.";
        return false;
    }

    measure = parsed;
    return true;
}

double bandValue (const juce::var& measure, const char* key)
{
    if (const auto* object = measure.getDynamicObject())
    {
        const auto bands = object->getProperty ("bands_db");

        if (const auto* bandsObject = bands.getDynamicObject())
        {
            const auto value = bandsObject->getProperty (key);

            if (! value.isVoid() && ! value.isUndefined())
                return static_cast<double> (value);
        }
    }

    return -999.0;
}

double numberField (const juce::var& object, const char* key, double fallback)
{
    if (const auto* dynamic = object.getDynamicObject())
    {
        const auto value = dynamic->getProperty (key);

        if (! value.isVoid() && ! value.isUndefined())
            return static_cast<double> (value);
    }

    return fallback;
}

double averageOf (std::initializer_list<double> values)
{
    double sum = 0.0;
    int count = 0;

    for (const auto value : values)
    {
        if (value > -900.0)
        {
            sum += value;
            ++count;
        }
    }

    return count > 0 ? sum / (double) count : 0.0;
}

int applyParams (MixRackChannel& channel, const juce::var& params)
{
    const auto* object = params.getDynamicObject();
    if (object == nullptr)
        return 0;

    int applied = 0;

    for (const auto& property : object->getProperties())
    {
        const auto id = property.name.toString();
        const auto value = property.value;
        const auto number = value.isBool() ? (value == juce::var (true) ? 1.0f : 0.0f)
                                           : (float) static_cast<double> (value);

        channel.setParam (id, number);
        ++applied;
    }

    return applied;
}

int applyParams (MasterRack& master, const juce::var& params)
{
    const auto* object = params.getDynamicObject();
    if (object == nullptr)
        return 0;

    int applied = 0;

    for (const auto& property : object->getProperties())
    {
        const auto id = property.name.toString();
        const auto value = property.value;
        const auto number = value.isBool() ? (value == juce::var (true) ? 1.0f : 0.0f)
                                           : (float) static_cast<double> (value);

        master.setParam (id, number);
        ++applied;
    }

    return applied;
}

std::vector<mixology::SelectedModuleInstance> instancesForPreset (int preset)
{
    // Technique module ids only — no engineer names. Preset names are
    // generic sonic goals (808 Weight, Vocal Presence, Subtractive, etc.).
    // Mapping: lowend_harmonic_stack (808), vocal_clarity_chain (presence),
    // vocal_emotion_chain (emotion), subtractive_foundation (subtractive),
    // master_contour (master).
    switch (juce::jlimit (0, 4, preset))
    {
        case 1:
            return {
                { "lowend_harmonic_stack", 0.90, true, false, BusTarget::bass808 },
                { "subtractive_foundation", 0.65, true, false, BusTarget::master }
            };

        case 2:
            return {
                { "vocal_clarity_chain", 0.85, true, false, BusTarget::lead_vocal },
                { "master_contour", 0.50, true, false, BusTarget::master }
            };

        case 3:
            return {
                { "subtractive_foundation", 0.90, true, false, BusTarget::master },
                { "vocal_emotion_chain", 0.55, true, false, BusTarget::lead_vocal }
            };

        case 4:
            return {
                { "vocal_emotion_chain", 0.80, true, false, BusTarget::lead_vocal },
                { "vocal_clarity_chain", 0.70, true, false, BusTarget::lead_vocal },
                { "lowend_harmonic_stack", 0.85, true, false, BusTarget::bass808 },
                { "subtractive_foundation", 0.75, true, false, BusTarget::master },
                { "master_contour", 0.55, true, false, BusTarget::master }
            };

        case 0:
        default:
            return {
                { "vocal_emotion_chain", 0.85, true, false, BusTarget::lead_vocal },
                { "subtractive_foundation", 0.70, true, false, BusTarget::lead_vocal },
                { "master_contour", 0.55, true, false, BusTarget::master }
            };
    }
}
} // namespace

juce::StringArray mixologyPresetNames()
{
    return { "VOCAL FOCUS", "808 WEIGHT", "VOCAL PRESENCE", "SUBTRACTIVE", "FULL CHAIN" };
}

MixologyReport applyMixologyPreset (AudioEngine& engine, int preset)
{
    MixologyReport report;
    const auto instances = instancesForPreset (preset);
    const auto resolution = mixology::resolveSignalChain (instances);
    const auto activeTracks = engine.getActiveTrackCount();

    for (const auto& entry : resolution.stepsByBus)
    {
        const auto bus = entry.first;
        const auto track = trackForBus (bus, activeTracks);

        // Master steps can always land; a bus with no host track is a skip,
        // never a silent write to track 0.
        if (bus != BusTarget::master && track < 0)
        {
            report.skipped += static_cast<int> (entry.second.size());
            continue;
        }

        for (const auto& step : entry.second)
        {
            mixology::OverlayInstanceSpec spec;
            std::string skipReason;

            if (! mixology::mapTechniqueToOverlay (step, spec, skipReason))
            {
                ++report.skipped;
                continue;
            }

            // The baked rack is the channel strip and master chain. A
            // technique that resolves to a delay or reverb return is not a
            // strip parameter, so it is reported as skipped rather than
            // pretending a send was built.
            if (spec.plugin != "Overlay Channel Strip")
            {
                ++report.skipped;
                continue;
            }

            if (bus == BusTarget::master)
            {
                for (const auto& param : spec.params)
                    engine.masterMix().setParam (juce::String (param.first), static_cast<float> (param.second));

                engine.masterMix().setParam ("eqOn", 1.0f);
            }
            else
            {
                auto& channel = engine.mixChannel (track);
                channel.setParam ("stripOn", 1.0f);
                channel.setParam ("eqOn", 1.0f);

                for (const auto& param : spec.params)
                    channel.setParam (juce::String (param.first), static_cast<float> (param.second));

                if (isVocalBus (bus))
                {
                    channel.setVocal (true);
                    channel.setParam ("openMicOn", 1.0f);
                    channel.setParam ("openMicDrive", 5.0f + 5.0f * static_cast<float> (step.intensity));
                    channel.setParam ("openMicMix", 0.22f + 0.20f * static_cast<float> (step.intensity));
                }
            }

            ++report.applied;
        }
    }

    for (const auto& audit : resolution.headroomAudit)
    {
        if (audit.first == BusTarget::master)
            engine.masterMix().setParam ("trimDb", static_cast<float> (audit.second.appliedAutoTrimDb));
        else if (const auto track = trackForBus (audit.first, activeTracks); track >= 0)
            engine.mixChannel (track).setParam ("outputDb", static_cast<float> (audit.second.appliedAutoTrimDb));
    }

    report.text = "MIXOLOGY / " + juce::String (report.applied) + " INSERTS APPLIED";

    if (report.skipped > 0)
        report.text += " / " + juce::String (report.skipped) + " SKIPPED";

    if (! resolution.conflicts.empty())
        report.text += " / " + juce::String (static_cast<int> (resolution.conflicts.size())) + " CONFLICTS RESOLVED";

    return report;
}

//==============================================================================
MixologyAutopilotReport runMixologyAutopilot (AudioEngine& engine,
                                             const juce::String& brief,
                                             const juce::String& audioPath,
                                             const juce::String& referencePath,
                                             double depth,
                                             double targetLufs)
{
    MixologyAutopilotReport report;
    depth = juce::jlimit (0.1, 1.0, depth);

    // Rendered passes live in a per-run temp directory and disappear on every
    // return path. A fixed filename collided between two app instances and left
    // files behind.
    juce::File autopilotTempDir;

    struct TempCleanup
    {
        juce::File& dir;
        ~TempCleanup()
        {
            if (dir != juce::File())
                dir.deleteRecursively();
        }
    } tempCleanup { autopilotTempDir };

    juce::File measuredFile (audioPath.trim());
    bool renderedSession = false;
    double renderEndBar = 8.0;
    int measuredPasses = 1;

    if (audioPath.trim().isEmpty())
    {
        // Render the session first. The same block path the device uses, so
        // Earshot hears exactly what the engineer hears.
        renderEndBar = engine.contentEndBar();
        if (! (renderEndBar > 1.0))
            renderEndBar = 8.0;

        autopilotTempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("nc-mixology-autopilot-" + juce::Uuid().toString());
        autopilotTempDir.createDirectory();
        auto temp = autopilotTempDir.getChildFile ("pass1.wav");

        juce::String renderError;
        if (! engine.renderToWav (temp, 1.0, renderEndBar, 24, renderError))
        {
            report.text = "MIXOLOGY AUTOPILOT / RENDER FAILED";
            report.summary = renderError.isNotEmpty() ? renderError : "The session could not be rendered.";
            return report;
        }

        measuredFile = temp;
        renderedSession = true;
    }

    if (! measuredFile.existsAsFile())
    {
        report.text = "MIXOLOGY AUTOPILOT / AUDIO NOT FOUND";
        report.summary = "No audio file at " + measuredFile.getFullPathName();
        return report;
    }

#if JUCE_WINDOWS
    // If the Mixology server is running, use its full deterministic pipeline:
    // Earshot + JEV strategy + Dev-Brain ranking/governance. The native path
    // below remains the offline fallback.
    const auto server = juce::SystemStats::getEnvironmentVariable ("NC_MIXOLOGY_SERVER", {}).trim();

    if (server.isNotEmpty())
    {
        auto* body = new juce::DynamicObject();
        body->setProperty ("brief", brief);
        body->setProperty ("audioPath", measuredFile.getFullPathName());
        body->setProperty ("referencePath", referencePath.trim());
        body->setProperty ("depth", depth);
        body->setProperty ("targetLufs", targetLufs);

        juce::String response;
        int status = 0;
        const auto transportError = postJsonToMixology (
            server.trimEnd() + "/api/autopilot",
            juce::JSON::toString (juce::var (body)),
            response,
            status);

        if (transportError.isEmpty() && status == 200)
        {
            const auto parsed = juce::JSON::parse (response);

            if (parsed.isObject())
            {
                const auto safeToApply = parsed.getProperty ("safeToApply", juce::var (true));
                const auto pipeline = parsed.getProperty ("pipeline", {});
                const auto pipelineSummary = pipeline.getProperty ("summary", {}).toString();
                const auto verdict = parsed.getProperty ("devBrain", {})
                                         .getProperty ("verdict", {});
                const auto verdictStatus = verdict.getProperty ("status", {}).toString();

                if (! (bool) safeToApply)
                {
                    report.text = "MIXOLOGY AUTOPILOT / DEV-BRAIN REJECTED";
                    report.summary = "Dev-Brain governance refused the bounded plan"
                                   + (verdictStatus.isNotEmpty() ? " (" + verdictStatus + ")." : ".")
                                   + (pipelineSummary.isNotEmpty() ? " " + pipelineSummary : "");
                    return report;
                }

                int applied = 0;
                int skipped = 0;
                const auto channels = parsed.getProperty ("channels", {});

                if (const auto* array = channels.getArray())
                {
                    for (const auto& item : *array)
                    {
                        const auto* object = item.getDynamicObject();
                        if (object == nullptr)
                            continue;

                        const auto bus = busFromName (object->getProperty ("bus").toString());
                        const auto params = object->getProperty ("params");

                        if (bus == BusTarget::master)
                        {
                            applied += applyParams (engine.masterMix(), params);
                            continue;
                        }

                        const auto track = trackForBus (bus, engine.getActiveTrackCount());

                        if (track < 0)
                        {
                            const auto* bag = params.getDynamicObject();
                            skipped += bag != nullptr ? (int) bag->getProperties().size() : 0;
                            continue;
                        }

                        auto& channel = engine.mixChannel (track);
                        channel.setParam ("stripOn", 1.0f);
                        applied += applyParams (channel, params);

                        if (isVocalBus (bus))
                        {
                            channel.setVocal (true);
                            channel.setParam ("openMicOn", 1.0f);
                            channel.setParam ("openMicDrive", 5.0f + 5.0f * (float) depth);
                            channel.setParam ("openMicMix", 0.18f + 0.18f * (float) depth);
                        }
                    }
                }

                applied += applyParams (engine.masterMix(), parsed.getProperty ("master", {}));

                report.applied = applied;
                report.skipped = skipped;
                report.ok = applied > 0;
                report.text = "MIXOLOGY AUTOPILOT / SERVER PIPELINE / "
                            + juce::String (applied) + " PARAMS";
                report.text << " / "
                            << juce::String ((double) parsed.getProperty ("targetLufs", -14.0), 1)
                            << " LUFS";
                report.summary = parsed.getProperty ("summary", {}).toString();
                report.assessment = parsed.getProperty ("assessment", {})
                                        .getProperty ("summary", {}).toString();

                if (pipelineSummary.isNotEmpty())
                    report.summary = report.summary + " " + pipelineSummary;

                if (verdictStatus.isNotEmpty())
                    report.summary = report.summary + " Dev-Brain: " + verdictStatus + ".";

                return report;
            }
        }
    }
#endif

    juce::var measure;
    juce::String measureError;

    if (! measureWithEarshot (measuredFile, measure, measureError))
    {
        report.text = "MIXOLOGY AUTOPILOT / EARSHOT FAILED";
        report.summary = measureError;
        return report;
    }

    juce::var referenceMeasure;
    bool haveReference = false;

    if (referencePath.trim().isNotEmpty())
    {
        juce::String referenceError;
        haveReference = measureWithEarshot (juce::File (referencePath.trim()), referenceMeasure, referenceError);

        if (! haveReference)
            report.summary = "Reference ignored: " + referenceError + " ";
    }

    static constexpr const char* bandKeys[] { "sub", "low", "lowmid", "mid", "presence", "air", "top" };
    static constexpr double targetShape[] { -18.0, -14.0, -11.0, -15.0, -18.0, -22.0, -26.0 };
    constexpr int numBands = 7;

    double measuredBands[numBands] {};
    double referenceBands[numBands] {};
    double moves[numBands] {};
    int present = 0;
    double measuredSum = 0.0;

    auto computeMoves = [&] (const juce::var& source, double passDepth)
    {
        present = 0;
        measuredSum = 0.0;

        for (int i = 0; i < numBands; ++i)
        {
            measuredBands[i] = bandValue (source, bandKeys[i]);
            referenceBands[i] = haveReference ? bandValue (referenceMeasure, bandKeys[i]) : -999.0;

            if (measuredBands[i] > -900.0)
            {
                measuredSum += measuredBands[i];
                ++present;
            }
        }

        if (present == 0)
            return;

        double targetMean = 0.0;
        for (const auto value : targetShape)
            targetMean += value;
        targetMean /= (double) numBands;

        const auto shift = measuredSum / (double) present - targetMean;

        for (int i = 0; i < numBands; ++i)
        {
            if (measuredBands[i] <= -900.0)
                continue;

            const auto delta = haveReference && referenceBands[i] > -900.0
                                   ? (referenceBands[i] - measuredBands[i]) * passDepth
                                   : (targetShape[i] + shift - measuredBands[i]) * passDepth;
            moves[i] = juce::jlimit (-7.0, 7.0, delta);
        }
    };

    computeMoves (measure, depth);

    if (present == 0)
    {
        report.text = "MIXOLOGY AUTOPILOT / NO BANDS";
        report.summary = "Earshot returned a measurement without spectral bands.";
        return report;
    }

    double lowMove = 0, lowMidMove = 0, midMove = 0, presenceMove = 0, airMove = 0;
    double lowGain = 0, lmfGain = 0, hmfGain = 0, highGain = 0;
    double hpf = 30;

    auto recomputeShape = [&]
    {
        lowMove = (moves[0] + moves[1]) * 0.5;
        lowMidMove = moves[2];
        midMove = moves[3];
        presenceMove = moves[4];
        airMove = (moves[5] + moves[6]) * 0.5;

        lowGain = juce::jlimit (-6.0, 6.0, lowMove * 0.75);
        lmfGain = juce::jlimit (-5.0, 5.0, lowMidMove * 0.65);
        hmfGain = juce::jlimit (-5.0, 5.0, (midMove * 0.35 + presenceMove * 0.65) * 0.75);
        highGain = juce::jlimit (-6.0, 6.0, airMove * 0.8);

        const auto sub = measuredBands[0];
        const auto low = measuredBands[1];
        hpf = sub > -900.0 && low > -900.0 && sub - low > 5.0
                  ? 45.0
                  : sub > -900.0 && sub < -35.0 ? 20.0 : 30.0;
    };

    recomputeShape();

    int compType = 0;
    if (brief.containsIgnoreCase ("punch") || brief.containsIgnoreCase ("drum")
        || brief.containsIgnoreCase ("kick") || brief.containsIgnoreCase ("attack"))
        compType = 1;
    else if (brief.containsIgnoreCase ("vocal") || brief.containsIgnoreCase ("vox")
             || brief.containsIgnoreCase ("opto") || brief.containsIgnoreCase ("intimate"))
        compType = 2;
    else if (brief.containsIgnoreCase ("bus") || brief.containsIgnoreCase ("glue")
             || brief.containsIgnoreCase ("cohes"))
        compType = 4;
    else if (brief.containsIgnoreCase ("clean") || brief.containsIgnoreCase ("transparent"))
        compType = 6;

    const auto peak = numberField (measure, "peak_dbfs", -3.0);
    const auto rms = numberField (measure, "rms_dbfs", -15.0);
    const auto dynamicRange = numberField (measure, "dynamic_range_db", juce::jmax (0.0, peak - rms));
    const auto crest = juce::jmax (0.0, juce::jmax (peak - rms, dynamicRange));
    const auto grTarget = juce::jlimit (2.0, 10.0, 2.5 + depth * 4.5 + juce::jmax (0.0, crest - 12.0) * 0.25);
    const auto compThresh = juce::jlimit (-42.0, -6.0, -18.0 - grTarget * 1.1);
    const auto compRatio = juce::jlimit (1.6, 8.0, 1.6 + depth * 3.4);
    const auto compAttack = compType == 1 ? 3.0 : compType == 2 ? 12.0 : 10.0;
    const auto compRelease = compType == 2 ? 350.0 : compType == 4 ? 300.0 : 150.0;

    int satMode = 0;
    if (brief.containsIgnoreCase ("vocal") || brief.containsIgnoreCase ("warm")
        || brief.containsIgnoreCase ("tube"))
        satMode = 1;
    else if (brief.containsIgnoreCase ("drum") || brief.containsIgnoreCase ("diode")
             || brief.containsIgnoreCase ("bite"))
        satMode = 2;

    const auto satDrive = juce::jlimit (2.0, 16.0, 3.0 + depth * 7.0);
    const auto satMix = juce::jlimit (0.08, 0.38, 0.10 + depth * 0.24);

    if (! (targetLufs > -60.0))
        targetLufs = depth >= 0.85 ? -9.0 : depth >= 0.65 ? -11.0 : depth >= 0.45 ? -14.0 : -16.0;
    targetLufs = juce::jlimit (-24.0, -6.0, targetLufs);

    auto makeChannelParams = [&] (double lowTrim, double lmfTrim, double hmfTrim,
                                  double highTrim, double hpfValue, int type,
                                  int mode, double driveScale, double mixScale)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("stripOn", 1.0);
        object->setProperty ("eqOn", 1.0);
        object->setProperty ("hpfOn", 1.0);
        object->setProperty ("hpfFreq", hpfValue);
        object->setProperty ("lowFreq", 90.0);
        object->setProperty ("lowGain", lowGain * lowTrim);
        object->setProperty ("lmfFreq", 700.0);
        object->setProperty ("lmfGain", lmfGain * lmfTrim - (type == 2 ? 0.6 : 0.0));
        object->setProperty ("hmfFreq", 3200.0);
        object->setProperty ("hmfGain", hmfGain * hmfTrim + (type == 2 ? 1.0 : 0.0));
        object->setProperty ("highFreq", 11000.0);
        object->setProperty ("highGain", highGain * highTrim);
        object->setProperty ("compOn", 1.0);
        object->setProperty ("compType", (double) type);
        object->setProperty ("compThresh", compThresh);
        object->setProperty ("compRatio", compRatio);
        object->setProperty ("compAttack", compAttack);
        object->setProperty ("compRelease", compRelease);
        object->setProperty ("compKnee", 6.0);
        object->setProperty ("compMix", 1.0);
        object->setProperty ("compAutoRelease", 1.0);
        object->setProperty ("compAutoGain", 0.0);
        object->setProperty ("satOn", 1.0);
        object->setProperty ("satMode", (double) mode);
        object->setProperty ("satDrive", satDrive * driveScale);
        object->setProperty ("satMix", satMix * mixScale);
        object->setProperty ("satDynamics", 0.18 + depth * 0.22);
        object->setProperty ("satToneBass", juce::jlimit (-0.5, 0.5, lowGain * 0.05));
        object->setProperty ("satToneMid", juce::jlimit (-0.4, 0.4, lmfGain * 0.04));
        object->setProperty ("satToneTreble", juce::jlimit (-0.5, 0.5, highGain * 0.06));
        object->setProperty ("satTonePresence", juce::jlimit (-0.5, 0.5, hmfGain * 0.05));
        object->setProperty ("satFeedback", 0.04 + depth * 0.08);
        object->setProperty ("satOversample", 1.0);
        return juce::var (object);
    };

    // Lead vocal: less low end, more presence, opto-ish control.
    const auto vocalParams = makeChannelParams (0.6, 0.7, 1.0, 1.0,
                                                juce::jmax (hpf, 60.0), 2, 1, 0.7, 0.7);
    // 808 / bass: keep the sub, tame the box, drive earlier.
    const auto bassParams = makeChannelParams (1.15, 0.4, 0.5, 0.5,
                                               20.0, 0, 0, 1.15, 1.15);
    // Drum bus: FET punch, diode edge.
    const auto drumParams = makeChannelParams (0.8, 0.5, 1.0, 1.0,
                                               30.0, 1, 2, 1.1, 1.1);

    const auto activeTracks = engine.getActiveTrackCount();
    auto applyBus = [&] (BusTarget bus, const juce::var& params, bool vocal)
    {
        if (bus == BusTarget::master)
            return applyParams (engine.masterMix(), params);

        const auto track = trackForBus (bus, activeTracks);

        if (track < 0)
        {
            const auto* object = params.getDynamicObject();
            report.skipped += object != nullptr ? (int) object->getProperties().size() : 0;
            return 0;
        }

        auto& channel = engine.mixChannel (track);
        channel.setParam ("stripOn", 1.0f);

        const auto count = applyParams (channel, params);

        if (vocal)
        {
            channel.setVocal (true);
            channel.setParam ("openMicOn", 1.0f);
            channel.setParam ("openMicDrive", 5.0f + 5.0f * (float) depth);
            channel.setParam ("openMicMix", 0.18f + 0.18f * (float) depth);
        }

        return count;
    };

    report.applied += applyBus (BusTarget::lead_vocal, vocalParams, true);
    report.applied += applyBus (BusTarget::bass808, bassParams, false);
    report.applied += applyBus (BusTarget::drum_bus, drumParams, false);

    // Bottom-up refinement: the master should hear the corrected channels,
    // not the pre-channel mix. At musical depth and above, render once more,
    // measure the result, and base the master pass on that evidence.
    if (renderedSession && depth >= 0.65)
    {
        auto second = autopilotTempDir.getChildFile ("pass2.wav");

        juce::String secondError;
        if (engine.renderToWav (second, 1.0, renderEndBar, 24, secondError))
        {
            juce::var secondMeasure;
            juce::String secondMeasureError;

            if (measureWithEarshot (second, secondMeasure, secondMeasureError))
            {
                measure = secondMeasure;
                computeMoves (measure, depth);
                recomputeShape();
                measuredPasses = 2;
            }
        }
    }

    auto* masterParams = new juce::DynamicObject();
    masterParams->setProperty ("eqOn", 1.0);
    masterParams->setProperty ("hpfOn", 1.0);
    masterParams->setProperty ("hpfFreq", 25.0);
    masterParams->setProperty ("lowFreq", 100.0);
    masterParams->setProperty ("lowGain", juce::jlimit (-2.0, 2.0, lowGain * 0.2));
    masterParams->setProperty ("midFreq", 1000.0);
    masterParams->setProperty ("midGain", juce::jlimit (-2.0, 2.0, lmfGain * 0.2));
    masterParams->setProperty ("highFreq", 10000.0);
    masterParams->setProperty ("highGain", juce::jlimit (-2.0, 2.0, highGain * 0.3));
    masterParams->setProperty ("deqOn", 1.0);
    masterParams->setProperty ("deq3On", 1.0);
    masterParams->setProperty ("deq3Freq", 3000.0);
    masterParams->setProperty ("deq3Q", 1.2);
    masterParams->setProperty ("deq3Thresh", -24.0);
    masterParams->setProperty ("deq3Range", juce::jlimit (-6.0, 0.0, presenceMove * 0.4));
    masterParams->setProperty ("satOn", airMove > 0.5 ? 1.0 : 0.0);
    masterParams->setProperty ("satMode", 1.0);
    masterParams->setProperty ("satDrive", juce::jlimit (2.0, 10.0, 3.0 + depth * 4.0));
    masterParams->setProperty ("satMix", 0.12);
    masterParams->setProperty ("satDynamics", 0.2);
    masterParams->setProperty ("satToneBass", juce::jlimit (-0.3, 0.3, lowGain * 0.04));
    masterParams->setProperty ("satToneTreble", juce::jlimit (-0.4, 0.4, highGain * 0.06));
    masterParams->setProperty ("maxOn", 1.0);
    masterParams->setProperty ("maxThresh", juce::jlimit (-6.0, 0.0, -1.0 - depth * 2.0));
    masterParams->setProperty ("maxMode", depth >= 0.8 ? 2.0 : 1.0);
    masterParams->setProperty ("maxOversample", 1.0);
    masterParams->setProperty ("ceiling", -1.0);
    masterParams->setProperty ("targetLufs", targetLufs);
    masterParams->setProperty ("automixOn", 0.0);
    masterParams->setProperty ("compare", 0.0);
    masterParams->setProperty ("trimDb", 0.0);

    report.applied += applyParams (engine.masterMix(), juce::var (masterParams));

    report.ok = report.applied > 0;
    report.text = "MIXOLOGY AUTOPILOT / " + juce::String (report.applied) + " PARAMS";
    report.text << " / " << juce::String (targetLufs, 1) << " LUFS";

    if (report.skipped > 0)
        report.text << " / " << juce::String (report.skipped) << " SKIPPED";

    const auto lufs = numberField (measure, "lufs", -70.0);
    const auto truePeak = numberField (measure, "true_peak_dbtp", -140.0);
    const auto measureSummary = measure.getProperty ("summary", juce::var()).toString();

    juce::String corrections;
    for (int i = 0; i < numBands; ++i)
    {
        if (measuredBands[i] <= -900.0 || std::abs (moves[i]) < 0.5)
            continue;

        if (corrections.isNotEmpty())
            corrections << ", ";

        corrections << bandKeys[i] << " " << (moves[i] >= 0.0 ? "+" : "")
                    << juce::String (moves[i], 1) << " dB";
    }

    report.summary = "Earshot " + juce::String (lufs, 1) + " LUFS, "
                   + juce::String (truePeak, 1) + " dBTP"
                   + (measuredPasses > 1 ? ", after a measured channel pass. " : ". ")
                   + (corrections.isNotEmpty() ? "Largest corrections: " + corrections + "."
                                               : juce::String ("Spectrum already close to target."));
    report.assessment = measureSummary.isNotEmpty() ? measureSummary
                                                    : "Earshot measured " + measuredFile.getFileName();

    if (! report.ok)
    {
        report.text = "MIXOLOGY AUTOPILOT / NOTHING APPLIED";
        report.summary = report.summary.isNotEmpty() ? report.summary
                                                     : "No track matched the Mixology buses.";
    }

    return report;
}
} // namespace nc
