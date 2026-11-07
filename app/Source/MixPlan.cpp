#include "MixPlan.h"

#include "AudioEngine.h"
#include "Session.h"

#include <algorithm>
#include <cmath>

#if JUCE_WINDOWS
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
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
// One lane per fader per session, with room to spare over the 16 tracks.
constexpr int maxLanes = 32;
constexpr int maxTextLength = 200;

#if JUCE_WINDOWS
struct WinHttpHandle
{
    explicit WinHttpHandle (HINTERNET handleToOwn) : handle (handleToOwn) {}

    ~WinHttpHandle()
    {
        if (handle != nullptr)
            WinHttpCloseHandle (handle);
    }

    HINTERNET get() const noexcept { return handle; }

    HINTERNET handle = nullptr;
};

/** Blocking JSON POST through WinHTTP. JUCE's URL backend returns no stream
    for this request on Windows, so the transport lives on the platform API.
    Returns an empty string on success; `status` receives the HTTP code. */
juce::String postJsonOverWinHttp (const juce::String& endpoint,
                                  const juce::String& payload,
                                  juce::String& response,
                                  int& status)
{
    const juce::URL url (endpoint);
    const auto host = url.getDomain();

    if (host.isEmpty())
        return "the model server address has no host";

    const auto secure = url.getScheme().equalsIgnoreCase ("https");
    const auto port = (INTERNET_PORT) (url.getPort() > 0 ? url.getPort()
                                                         : (secure ? 443 : 80));

    juce::String path = "/" + url.getSubPath();

    if (url.getQueryString().isNotEmpty())
        path << url.getQueryString();

    WinHttpHandle session (WinHttpOpen (L"NC Mix Assistant/1.0",
                                       WINHTTP_ACCESS_TYPE_NO_PROXY,
                                       WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0));

    if (session.get() == nullptr)
        return "WinHTTP could not open a session (" + juce::String ((int) GetLastError()) + ")";

    WinHttpSetTimeouts (session.get(), 15000, 15000, 300000, 300000);

    WinHttpHandle connection (WinHttpConnect (session.get(), host.toWideCharPointer(), port, 0));

    if (connection.get() == nullptr)
        return "WinHTTP could not reach " + host + " (" + juce::String ((int) GetLastError()) + ")";

    WinHttpHandle request (WinHttpOpenRequest (connection.get(), L"POST", path.toWideCharPointer(),
                                               nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               secure ? WINHTTP_FLAG_SECURE : 0));

    if (request.get() == nullptr)
        return "WinHTTP could not create the request (" + juce::String ((int) GetLastError()) + ")";

    static constexpr wchar_t headers[] = L"Content-Type: application/json\r\nAccept: application/json\r\n";
    const auto bodySize = (DWORD) payload.getNumBytesAsUTF8();

    if (! WinHttpSendRequest (request.get(), headers, (DWORD) -1,
                              (LPVOID) payload.toRawUTF8(), bodySize,
                              bodySize, 0))
        return "WinHTTP could not send the request (" + juce::String ((int) GetLastError()) + ")";

    if (! WinHttpReceiveResponse (request.get(), nullptr))
        return "WinHTTP did not receive a response (" + juce::String ((int) GetLastError()) + ")";

    DWORD httpStatus = 0;
    DWORD statusSize = sizeof (httpStatus);

    if (! WinHttpQueryHeaders (request.get(),
                               WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX,
                               &httpStatus, &statusSize, WINHTTP_NO_HEADER_INDEX))
        return "WinHTTP could not read the status (" + juce::String ((int) GetLastError()) + ")";

    status = (int) httpStatus;
    response.clear();

    for (;;)
    {
        DWORD available = 0;

        if (! WinHttpQueryDataAvailable (request.get(), &available))
            return "WinHTTP could not read the response (" + juce::String ((int) GetLastError()) + ")";

        if (available == 0)
            break;

        std::vector<char> buffer ((size_t) available);
        DWORD received = 0;

        if (! WinHttpReadData (request.get(), buffer.data(), available, &received))
            return "WinHTTP could not read the response (" + juce::String ((int) GetLastError()) + ")";

        response.append (buffer.data(), (int) received);
    }

    return {};
}
#endif

juce::String laneName (int index)
{
    return "lane " + juce::String (index + 1);
}

juce::String paramName (AutomationParam param)
{
    return param == AutomationParam::Pan ? "pan" : "volume";
}

bool readNumber (const juce::var& value, double& out)
{
    if (! value.isDouble() && ! value.isInt() && ! value.isInt64())
        return false;

    out = static_cast<double> (value);
    return std::isfinite (out);
}

juce::String readText (const juce::var& value)
{
    return value.isString() ? value.toString().trim().substring (0, maxTextLength)
                            : juce::String();
}

/** Accepts both the compact [beat, value] form and a named object, so a model
    that drifts between the two still lands. */
bool readPoint (const juce::var& raw, double& beats, double& value)
{
    if (auto* array = raw.getArray())
        return array->size() == 2 && readNumber ((*array)[0], beats) && readNumber ((*array)[1], value);

    if (auto* object = raw.getDynamicObject())
        return readNumber (object->getProperty ("beat"), beats)
               && readNumber (object->getProperty ("value"), value);

    return false;
}

bool parseLane (const juce::var& raw, int activeTracks, MixPlanLane& lane,
                juce::String& reason, std::vector<juce::String>& notes)
{
    auto* object = raw.getDynamicObject();

    if (object == nullptr)
    {
        reason = "a lane must be an object";
        return false;
    }

    double trackValue = 0.0;

    if (! readNumber (object->getProperty ("track"), trackValue))
    {
        reason = "a lane needs a numeric track";
        return false;
    }

    const auto track = static_cast<int> (std::round (trackValue));

    if (std::abs (trackValue - static_cast<double> (track)) > 1.0e-9)
    {
        reason = "track must be a whole number";
        return false;
    }

    if (track < 0 || track >= activeTracks)
    {
        reason = "track " + juce::String (track) + " is outside 0.."
                 + juce::String (juce::jmax (0, activeTracks - 1));
        return false;
    }

    const auto name = readText (object->getProperty ("param")).toLowerCase();

    if (name == "volume")
        lane.param = AutomationParam::Volume;
    else if (name == "pan")
        lane.param = AutomationParam::Pan;
    else
    {
        reason = "param must be \"volume\" or \"pan\"";
        return false;
    }

    lane.track = track;
    lane.why = readText (object->getProperty ("why"));

    auto* points = object->getProperty ("points").getArray();

    if (points == nullptr)
    {
        reason = "a lane needs a points array";
        return false;
    }

    if (points->size() > AutomationLane::maxPoints)
    {
        reason = "more than " + juce::String (AutomationLane::maxPoints) + " points";
        return false;
    }

    const auto low = lane.param == AutomationParam::Volume ? 0.0 : -1.0;
    constexpr auto high = 1.0;

    for (const auto& rawPoint : *points)
    {
        double beats = 0.0;
        double value = 0.0;

        if (! readPoint (rawPoint, beats, value))
        {
            reason = "a point must be [beat, value]";
            return false;
        }

        if (beats < 0.0)
        {
            reason = "a beat cannot be negative";
            return false;
        }

        // Overshoot is ordinary model jitter, so it is clamped and reported
        // rather than thrown away; a fader outside its own range is not.
        if (value < low || value > high)
        {
            const auto clamped = juce::jlimit (low, high, value);

            notes.push_back ("track " + juce::String (track + 1) + " " + paramName (lane.param)
                             + ": clamped " + juce::String (value, 3) + " to "
                             + juce::String (clamped, 3));

            value = clamped;
        }

        lane.points.push_back ({ beats, static_cast<float> (value) });
    }

    return true;
}
} // namespace

MixPlanValidation parseMixPlan (const juce::var& root, int activeTracks)
{
    MixPlanValidation result;

    auto* object = root.getDynamicObject();

    if (object == nullptr)
    {
        result.error = "a mix plan must be a JSON object";
        return result;
    }

    result.plan.title = readText (object->getProperty ("title"));

    auto* lanes = object->getProperty ("lanes").getArray();

    if (lanes == nullptr)
    {
        result.error = "a mix plan needs a lanes array";
        return result;
    }

    if (lanes->isEmpty())
    {
        result.error = "the plan has no lanes to apply";
        return result;
    }

    if (lanes->size() > maxLanes)
    {
        result.error = "a plan can hold " + juce::String (maxLanes) + " lanes at most";
        return result;
    }

    for (int index = 0; index < lanes->size(); ++index)
    {
        MixPlanLane lane;
        juce::String reason;

        if (! parseLane ((*lanes)[index], activeTracks, lane, reason, result.notes))
        {
            result.rejected.push_back ({ laneName (index), reason });
            continue;
        }

        const auto duplicate = std::find_if (result.plan.lanes.begin(), result.plan.lanes.end(),
                                             [&lane] (const MixPlanLane& kept)
                                             {
                                                 return kept.track == lane.track && kept.param == lane.param;
                                             });

        if (duplicate != result.plan.lanes.end())
        {
            result.rejected.push_back ({ laneName (index),
                                         "track " + juce::String (lane.track + 1) + " already has a "
                                             + paramName (lane.param) + " lane" });
            continue;
        }

        result.plan.lanes.push_back (std::move (lane));
    }

    if (result.plan.lanes.empty())
        result.error = "no lane survived validation";

    return result;
}

MixPlanValidation parseMixPlanJson (const juce::String& text, int activeTracks)
{
    if (text.trim().isEmpty())
    {
        MixPlanValidation result;
        result.error = "the plan text is empty";
        return result;
    }

    juce::var root;
    const auto parsed = juce::JSON::parse (text, root);

    if (parsed.failed())
    {
        MixPlanValidation result;
        result.error = "the plan is not valid JSON: " + parsed.getErrorMessage();
        return result;
    }

    return parseMixPlan (root, activeTracks);
}

juce::String MixPlanValidation::summary() const
{
    if (error.isNotEmpty())
        return error;

    auto text = juce::String (static_cast<int> (plan.lanes.size()))
                + (plan.lanes.size() == 1 ? " lane" : " lanes");

    if (! rejected.empty())
        text << ", " << static_cast<int> (rejected.size()) << " rejected ("
             << rejected.front().where << ": " << rejected.front().reason << ")";

    return text;
}

MixPlanReport executeMixPlan (AudioEngine& engine, const MixPlan& plan)
{
    MixPlanReport report;

    if (plan.title.isNotEmpty())
        report.notes.add ("plan: " + plan.title);

    const auto activeTracks = engine.getActiveTrackCount();

    for (const auto& lane : plan.lanes)
    {
        const auto label = "track " + juce::String (lane.track + 1) + " " + paramName (lane.param);

        // The session can shrink between validation and execution; a lane that
        // no longer has a track is reported, never folded onto another one.
        if (lane.track < 0 || lane.track >= activeTracks)
        {
            report.notes.add (label + ": skipped, that track is no longer active");
            continue;
        }

        if (lane.points.empty())
        {
            engine.clearTrackAutomation (lane.track, lane.param);
            ++report.cleared;
            report.notes.add (label + ": cleared");
            continue;
        }

        AutomationLane automation;
        automation.setPoints (lane.points);
        engine.setTrackAutomation (lane.track, lane.param, automation);
        ++report.applied;

        const auto count = automation.numPoints();
        report.notes.add (label + ": " + juce::String (count) + (count == 1 ? " point" : " points")
                          + (lane.why.isNotEmpty() ? " (" + lane.why + ")" : ""));
    }

    return report;
}
juce::String extractMixPlanJson (const juce::String& reply)
{
    for (int index = 0; index < reply.length(); ++index)
    {
        if (reply[index] != '{')
            continue;

        int depth = 0;
        bool insideString = false;
        bool escaped = false;

        for (int cursor = index; cursor < reply.length(); ++cursor)
        {
            const auto character = reply[cursor];

            if (insideString)
            {
                if (escaped)
                    escaped = false;
                else if (character == '\\')
                    escaped = true;
                else if (character == '"')
                    insideString = false;

                continue;
            }

            if (character == '"')
                insideString = true;
            else if (character == '{')
                ++depth;
            else if (character == '}')
            {
                --depth;

                if (depth == 0)
                {
                    const auto candidate = reply.substring (index, cursor + 1);
                    juce::var parsed;

                    if (! juce::JSON::parse (candidate, parsed).failed())
                        return candidate;

                    break;
                }
            }
        }
    }

    return {};
}

juce::String buildMixPlanPrompt (const juce::String& brief, int activeTracks)
{
    const auto& tracks = sessionTracks();
    juce::String trackList;

    for (int index = 0; index < activeTracks && index < (int) tracks.size(); ++index)
        trackList << juce::String (index) << ": " << tracks[index].name
                  << " [" << tracks[index].kind << "] "
                  << tracks[index].instrument << "/" << tracks[index].preset << "\n";

    juce::String prompt;
    prompt << "You are a careful mix engineer. Reply with only a JSON object, no markdown, no explanation.\n";
    prompt << "Schema:\n";
    prompt << R"({"title":string,"lanes":[{"track":integer,"param":"volume"|"pan","points":[[beat,gain],...],"why":string}]})" << "\n";
    prompt << "Beats are quarter notes from bar 1. Volume is linear gain 0..1. Pan is -1..1. "
           << "Empty points clears that fader and returns it to manual. Keep 2-5 points per lane.\n";
    prompt << "Active tracks: " << activeTracks << " (valid indices 0.."
           << juce::jmax (0, activeTracks - 1) << "). Track index must name one of these tracks.\n";
    prompt << "Session tracks (zero-based index, role, instrument/preset):\n";
    prompt << (trackList.isEmpty() ? "(none)\n" : trackList);
    prompt << "Goal: " << brief << "\n";
    prompt << "Return exactly 3 lanes, each with 2-3 points, and keep every why under 8 words. "
              "Mix the relevant tracks toward the goal. Only JSON.";
    return prompt;
}

MixPlanFetchResult requestMixPlanFromQwen (const juce::String& serverUrl,
                                          const juce::String& prompt,
                                          int activeTracks)
{
    MixPlanFetchResult result;

    juce::String endpoint = serverUrl.trim();

    if (! endpoint.endsWithIgnoreCase ("/v1/chat/completions"))
    {
        endpoint = endpoint.trimEnd();

        while (endpoint.endsWithChar ('/'))
            endpoint = endpoint.dropLastCharacters (1);

        endpoint << "/v1/chat/completions";
    }

    juce::URL url (endpoint);

    auto* message = new juce::DynamicObject();
    message->setProperty ("role", "user");
    message->setProperty ("content", prompt);

    juce::Array<juce::var> messages;
    messages.add (juce::var (message));

    auto* kwargs = new juce::DynamicObject();
    kwargs->setProperty ("enable_thinking", false);

    auto* body = new juce::DynamicObject();
    body->setProperty ("model", "qwen3.5-4b");
    body->setProperty ("messages", messages);
    body->setProperty ("temperature", 0.4);
    body->setProperty ("stream", false);
    body->setProperty ("top_p", 0.9);
    body->setProperty ("max_tokens", 360);
    body->setProperty ("chat_template_kwargs", juce::var (kwargs));

    const auto payload = juce::JSON::toString (juce::var (body), true);

    juce::String response;
    int status = 0;

#if JUCE_WINDOWS
    const auto transportError = postJsonOverWinHttp (endpoint, payload, response, status);

    if (transportError.isNotEmpty())
    {
        result.error = transportError;
        return result;
    }
#else
    url = url.withPOSTData (payload);

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
        .withExtraHeaders ("Content-Type: application/json\r\nAccept: application/json\r\n")
        .withConnectionTimeoutMs (15000)
        .withStatusCode (&status);

    auto stream = url.createInputStream (options);

    if (stream == nullptr)
    {
        result.error = "could not connect to " + serverUrl;
        return result;
    }

    response = stream->readEntireStreamAsString();
#endif

    if (status != 200)
    {
        result.error = "model server returned HTTP " + juce::String (status) + ": "
                      + response.substring (0, 240);
        return result;
    }

    juce::var root;
    const auto parsed = juce::JSON::parse (response, root);

    if (parsed.failed())
    {
        result.error = "model response was not JSON: " + parsed.getErrorMessage();
        return result;
    }

    if (auto* object = root.getDynamicObject())
    {
        if (auto* choices = object->getProperty ("choices").getArray())
        {
            if (! choices->isEmpty())
            {
                if (auto* choice = (*choices)[0].getDynamicObject())
                {
                    juce::String content;

                    if (auto* messageObject = choice->getProperty ("message").getDynamicObject())
                        content = messageObject->getProperty ("content").toString();
                    else
                        content = choice->getProperty ("text").toString();

                    const auto planJson = extractMixPlanJson (content);

                    if (planJson.isEmpty())
                    {
                        result.error = "model replied without a plan object";
                        return result;
                    }

                    result.validation = parseMixPlanJson (planJson, activeTracks);
                    return result;
                }
            }
        }

        result.error = "model returned no choices";
        return result;
    }

    result.error = "unrecognised model response";
    return result;
}

} // namespace nc
