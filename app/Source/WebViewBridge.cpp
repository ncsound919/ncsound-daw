// WebViewBridge: JUCE WebView2 host for the React pitch UI (VST DAW/ui).
//
// buildWebOptions() registers every native function the React UI can call.
// Each call is one row in the routing table below; the entries forward to
// AudioEngine and the MainComponent pitch setters. See
// ui/scripts/gen-native-routes.mjs which extracts this table into
// ui/src/lib/nativeRoutes.generated.ts.
//
// Native functions (see ui/src/lib/bridge.ts):
//   getState()                     -> AppState (bpm + pitch + tracks)
//   getPitch()                     -> { globalPitch, tracks[] pitch }
//   setGlobalPitch(patch)          -> AppState
//   setTrackPitch(track, patch)    -> AppState
//   setInstrumentPitch(track, patch) -> AppState
// Projections: getState/getPitch are read-only (non-mutating).
#include "MainComponent.h"

#include <VstawWeb.h>

#include <cmath>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace nc
{
namespace
{
// Debug builds only: a release build must not let an environment variable
// point the WebView -- and with it the full native bridge -- at another origin.
juce::String devServerUrl()
{
#if JUCE_DEBUG
    const auto url = juce::SystemStats::getEnvironmentVariable ("VSTDAW_WEB_DEV", {});
    if (url.startsWith ("http://localhost:") || url.startsWith ("http://127.0.0.1:"))
        return url;
#endif
    return {};
}

juce::String mimeFor (const juce::String& path)
{
    const auto ext = path.fromLastOccurrenceOf (".", false, false).toLowerCase();
    if (ext == "html") return "text/html";
    if (ext == "js") return "text/javascript";
    if (ext == "mjs") return "text/javascript";
    if (ext == "css") return "text/css";
    if (ext == "json") return "application/json";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "ico") return "image/x-icon";
    if (ext == "woff2") return "font/woff2";
    if (ext == "woff") return "font/woff";
    if (ext == "ttf") return "font/ttf";
    if (ext == "otf") return "font/otf";
    if (ext == "map") return "application/json";
    if (ext == "wasm") return "application/wasm";
    return "application/octet-stream";
}

double finiteOr (const juce::var& value, double fallback)
{
    // juce::var from JSON may be int/double/bool/string.
    const auto& s = value.toString();
    (void) s;
    const double d = (double) value;
    return std::isfinite (d) ? d : fallback;
}

nc::engine::PitchStack pitchFromVar (const juce::var& patch, const nc::engine::PitchStack& fallback)
{
    auto* obj = patch.getDynamicObject();
    nc::engine::PitchStack p = fallback;
    if (obj == nullptr)
        return p;
    if (obj->hasProperty ("transpose"))
        p.transpose = (int) std::round (finiteOr (obj->getProperty ("transpose"), p.transpose));
    if (obj->hasProperty ("pitchCents"))
        p.pitchCents = (float) finiteOr (obj->getProperty ("pitchCents"), p.pitchCents);
    if (obj->hasProperty ("tuningHz"))
        p.tuningHz = (float) finiteOr (obj->getProperty ("tuningHz"), p.tuningHz);
    p.clamp();
    return p;
}

juce::var pitchToVar (const nc::engine::PitchStack& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("transpose", p.transpose);
    o->setProperty ("pitchCents", (double) p.pitchCents);
    o->setProperty ("tuningHz", (double) p.tuningHz);
    return juce::var (o);
}
} // namespace

juce::var MainComponent::buildPitch() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("globalPitch", pitchToVar (engine.globalPitchStack()));
    juce::Array<juce::var> tracks;
    const auto active = engine.getActiveTrackCount();
    for (int i = 0; i < active; ++i)
    {
        auto* t = new juce::DynamicObject();
        t->setProperty ("index", i);
        t->setProperty ("pitch", pitchToVar (engine.trackPitchStack (i)));
        t->setProperty ("instrumentPitch", pitchToVar (engine.instrumentPitchStack (i)));
        tracks.add (juce::var (t));
    }
    o->setProperty ("tracks", tracks);
    return juce::var (o);
}

juce::var MainComponent::buildTransport() const
{
    auto* o = new juce::DynamicObject();
    const auto bpm = juce::jmax (1.0, engine.getBpm());
    const auto position = engine.positionSeconds();

    o->setProperty ("playing", engine.isPlaying());
    o->setProperty ("positionSeconds", position);
    o->setProperty ("positionBar", 1.0 + position / (240.0 / bpm));
    o->setProperty ("bpm", bpm);
    o->setProperty ("loopEnabled", engine.isLoopEnabled());
    o->setProperty ("metronome", engine.isMetronomeOn());
    o->setProperty ("masterPeakLeft", (double) engine.masterPeakLeft());
    o->setProperty ("masterPeakRight", (double) engine.masterPeakRight());
    o->setProperty ("masterDim", engine.isMasterDimOn());
    o->setProperty ("masterMono", engine.isMasterMonoOn());
    o->setProperty ("loopStartBar", engine.getLoopStartBar());
    o->setProperty ("loopEndBar", engine.getLoopEndBar());

    juce::Array<juce::var> peaks;
    const auto active = engine.getActiveTrackCount();
    for (int i = 0; i < active; ++i)
        peaks.add ((double) engine.trackPeak (i));
    o->setProperty ("trackPeaks", peaks);

    return juce::var (o);
}

juce::var MainComponent::buildState() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("bpm", engine.getBpm());
    o->setProperty ("selectedTrack", selectedTrack);
    o->setProperty ("globalPitch", pitchToVar (engine.globalPitchStack()));
    o->setProperty ("projectDirty", false);
    o->setProperty ("transport", buildTransport());

    juce::Array<juce::var> clips;
    const auto secondsPerBar = 240.0 / juce::jmax (1.0, engine.getBpm());

    for (const auto& clip : engine.timelineClips())
    {
        auto* c = new juce::DynamicObject();
        c->setProperty ("track", clip.track);
        c->setProperty ("startBar", clip.startBar);
        c->setProperty ("lengthBars", clip.lengthBars);
        c->setProperty ("assetId", clip.assetId);
        c->setProperty ("gainDb", (double) clip.gainDb);
        c->setProperty ("name", engine.assetName (clip.assetId));
        c->setProperty ("fadeInBars", (double) clip.fadeInBars);
        c->setProperty ("fadeOutBars", (double) clip.fadeOutBars);

        // The waveform covers what the clip plays, not the whole asset: a clip
        // trimmed into the middle of a take must show that middle.
        juce::Array<juce::var> envelope;
        const auto peaks = engine.assetPeakEnvelope (clip.assetId, clip.sourceOffsetSeconds,
                                                    clip.lengthBars * secondsPerBar, 96);
        for (const auto peak : peaks)
            envelope.add ((double) peak);
        c->setProperty ("peaks", envelope);

        clips.add (juce::var (c));
    }
    o->setProperty ("clips", clips);
    juce::Array<juce::var> tracks;
    const auto active = engine.getActiveTrackCount();
    for (int i = 0; i < active; ++i)
    {
        auto tp = engine.trackPitchStack (i);
        auto ip = engine.instrumentPitchStack (i);
        const auto gain = engine.trackGain (i);
        auto* t = new juce::DynamicObject();
        t->setProperty ("index", i);
        t->setProperty ("name", trackName (i));
        t->setProperty ("instrumentName", engine.trackInstrumentName (i));
        t->setProperty ("gainDb", juce::Decibels::gainToDecibels ((double) gain, -60.0));
        t->setProperty ("pan", (double) engine.trackPan (i));
        t->setProperty ("muted", engine.trackMuted (i));
        t->setProperty ("soloed", engine.trackSolo (i));
        t->setProperty ("transpose", tp.transpose);
        t->setProperty ("sendA", (double) engine.trackSendA (i));
        t->setProperty ("sendB", (double) engine.trackSendB (i));
        t->setProperty ("armed", engine.isRecordArmed (i));

        // Insert rack: the four user slots only. Slot 0 is the baked native
        // strip, which the console reports separately as the track's channel.
        juce::Array<juce::var> inserts;
        for (int slot = 0; slot < 4; ++slot)
        {
            auto* s = new juce::DynamicObject();
            s->setProperty ("name", engine.trackInsertName (i, slot));
            s->setProperty ("missing", engine.trackInsertIsMissing (i, slot));
            inserts.add (juce::var (s));
        }
        t->setProperty ("inserts", inserts);
        t->setProperty ("pitchCents", (double) tp.pitchCents);
        t->setProperty ("tuningHz", (double) tp.tuningHz);
        t->setProperty ("instrumentPitch", pitchToVar (ip));
        tracks.add (juce::var (t));
    }
    o->setProperty ("tracks", tracks);
    return juce::var (o);
}

void MainComponent::pushState()
{
    if (webView != nullptr)
        webView->emitEventIfBrowserIsVisible ("stateChanged", buildState());
}

juce::WebBrowserComponent::Options MainComponent::buildWebOptions()
{
    using Options = juce::WebBrowserComponent::Options;
    using Args = juce::Array<juce::var>;
    using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;
    using NativeFunction = juce::WebBrowserComponent::NativeFunction;

    const auto dev = devServerUrl();

    auto opts = Options {}
                    .withNativeIntegrationEnabled()
                    .withResourceProvider ([this] (const juce::String& path)
                                           { return getWebResource (path); },
                                           dev.isNotEmpty()
                                               ? std::optional<juce::String> (dev)
                                               : std::nullopt);

#if JUCE_WINDOWS
    opts = opts.withBackend (Options::Backend::webview2);
#endif

    // Native-function routing table. Adding a call the UI can make is adding
    // one row here. The order matches ui/src/lib/nativeRoutes.generated.ts.
    // getState/getPitch are read-only; the rest mutate the engine and echo state.
    const std::pair<const char*, NativeFunction> routes[] =
    {
        { "getState", [this] (const Args&, Completion done)
          { done (buildState()); } },
        { "getPitch", [this] (const Args&, Completion done)
          { done (buildPitch()); } },
        { "setGlobalPitch", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
              {
                  auto p = pitchFromVar (a[0], engine.globalPitchStack());
                  engine.setGlobalPitch (p);
                  setStatus ("GLOBAL PITCH / " + juce::String (p.transpose) + " st / "
                             + juce::String ((double) p.pitchCents, 0) + " ct / "
                             + juce::String ((double) p.tuningHz, 1) + " Hz");
              }
              done (buildState());
          } },
        { "setTrackPitch", [this] (const Args& a, Completion done)
          {
              if (a.size() >= 2)
              {
                  const auto idx = (int) a[0];
                  auto p = pitchFromVar (a[1], engine.trackPitchStack (idx));
                  engine.setTrackPitch (idx, p);
                  setStatus ("TRACK PITCH / " + trackName (idx) + " / "
                             + juce::String (p.transpose) + " st");
              }
              done (buildState());
          } },
        { "setInstrumentPitch", [this] (const Args& a, Completion done)
          {
              if (a.size() >= 2)
              {
                  const auto idx = (int) a[0];
                  auto p = pitchFromVar (a[1], engine.instrumentPitchStack (idx));
                  engine.setInstrumentPitch (idx, p);
                  setStatus ("INSTRUMENT PITCH / " + trackName (idx) + " / "
                             + juce::String (p.transpose) + " st");
              }
              done (buildState());
          } },
        { "getTransport", [this] (const Args&, Completion done)
          { done (buildTransport()); } },
        { "setPlaying", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
                  engine.setPlaying ((bool) a[0]);
              done (buildState());
          } },
        { "setPositionBar", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
              {
                  const auto bar = juce::jmax (1.0, finiteOr (a[0], 1.0));
                  engine.setPositionSeconds ((bar - 1.0) * 240.0 / juce::jmax (1.0, engine.getBpm()));
              }
              done (buildState());
          } },
        { "setBpm", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
              {
                  engine.setBpm (finiteOr (a[0], engine.getBpm()));
                  setStatus ("TEMPO / " + juce::String (engine.getBpm(), 1) + " BPM");
              }
              done (buildState());
          } },
        { "setLoopEnabled", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
                  engine.setLoopEnabled ((bool) a[0]);
              done (buildState());
          } },
        { "setMetronome", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
                  engine.setMetronome ((bool) a[0]);
              done (buildState());
          } },
        { "setTrackGain", [this] (const Args& a, Completion done)
          {
              if (a.size() >= 2)
              {
                  const auto db = juce::jlimit (-60.0, 6.0, finiteOr (a[1], 0.0));
                  engine.setTrackGain ((int) a[0],
                                       (float) juce::Decibels::decibelsToGain (db, -60.0));
              }
              done (buildState());
          } },
        { "setTrackPan", [this] (const Args& a, Completion done)
          {
              if (a.size() >= 2)
                  engine.setTrackPan ((int) a[0],
                                      (float) juce::jlimit (-1.0, 1.0, finiteOr (a[1], 0.0)));
              done (buildState());
          } },
        { "setTrackMuted", [this] (const Args& a, Completion done)
          {
              if (a.size() >= 2)
                  engine.setTrackMuted ((int) a[0], (bool) a[1]);
              done (buildState());
          } },
        { "setTrackSolo", [this] (const Args& a, Completion done)
          {
              if (a.size() >= 2)
                  engine.setTrackSolo ((int) a[0], (bool) a[1]);
              done (buildState());
          } },
        { "setMasterGain", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
              {
                  const auto db = juce::jlimit (-60.0, 6.0, finiteOr (a[0], 0.0));
                  engine.setMasterGain ((float) juce::Decibels::decibelsToGain (db, -60.0));
              }
              done (buildState());
          } },
        { "setSelectedTrack", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
                  setSelectedTrack ((int) a[0]);
              done (buildState());
          } },
        { "setActiveTrackCount", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
              {
                  const auto count = juce::jlimit (1, AudioEngine::trackCount, (int) a[0]);
                  engine.setActiveTrackCount (count);
                  arrange.refresh();
                  mixer.resized();
                  setSelectedTrack (juce::jmin (selectedTrack, count - 1));
              }
              done (buildState());
          } },
        { "setTrackSendA", [this] (const Args& a, Completion done)
          {
              if (a.size() >= 2)
                  engine.setTrackSendA ((int) a[0], (float) juce::jlimit (0.0, 1.0, finiteOr (a[1], 0.0)));
              done (buildState());
          } },
        { "setTrackSendB", [this] (const Args& a, Completion done)
          {
              if (a.size() >= 2)
                  engine.setTrackSendB ((int) a[0], (float) juce::jlimit (0.0, 1.0, finiteOr (a[1], 0.0)));
              done (buildState());
          } },
        { "setRecordArmed", [this] (const Args& a, Completion done)
          {
              if (a.size() >= 2)
                  engine.setRecordArmed ((int) a[0], (bool) a[1]);
              done (buildState());
          } },
        { "setMasterDim", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
                  engine.setMasterDim ((bool) a[0]);
              done (buildState());
          } },
        { "setMasterMono", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
                  engine.setMasterMono ((bool) a[0]);
              done (buildState());
          } },
        { "setClipEdit", [this] (const Args& a, Completion done)
          {
              if (a.size() >= 2)
                  arrange.webEditClip ((int) a[0], a[1]);
              pushState();
              done (buildState());
          } },
        { "setClipSplit", [this] (const Args& a, Completion done)
          {
              if (a.size() >= 2)
                  arrange.webSplitClip ((int) a[0], finiteOr (a[1], 1.0));
              pushState();
              done (buildState());
          } },
        { "setClipDuplicate", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
                  arrange.webDuplicateClip ((int) a[0]);
              pushState();
              done (buildState());
          } },
        { "setClipDelete", [this] (const Args& a, Completion done)
          {
              if (! a.isEmpty())
                  arrange.webDeleteClip ((int) a[0]);
              pushState();
              done (buildState());
          } },
        { "setLoopBars", [this] (const Args& a, Completion done)
          {
              if (a.size() >= 2)
                  arrange.webSetLoopRange (finiteOr (a[0], 1.0), finiteOr (a[1], 17.0));
              pushState();
              done (buildState());
          } },
    };

    for (const auto& route : routes)
        opts = opts.withNativeFunction (route.first, route.second);

    return opts;
}

std::optional<juce::WebBrowserComponent::Resource>
MainComponent::getWebResource (const juce::String& path)
{
    auto name = path;
    if (name.isEmpty() || name == "/")
        name = "index.html";
    else
        name = name.fromLastOccurrenceOf ("/", false, false);

    for (int i = 0; i < VstawWeb::namedResourceListSize; ++i)
    {
        if (juce::String (VstawWeb::originalFilenames[i]).equalsIgnoreCase (name))
        {
            int size = 0;
            const char* data = VstawWeb::getNamedResource (VstawWeb::namedResourceList[i], size);
            if (data == nullptr)
                break;
            std::vector<std::byte> bytes ((size_t) size);
            std::memcpy (bytes.data(), data, (size_t) size);
            return juce::WebBrowserComponent::Resource { std::move (bytes), mimeFor (name) };
        }
    }
    return std::nullopt;
}

void MainComponent::setPitchViewOpen (bool shouldOpen)
{
    pitchOpen = shouldOpen;
    if (webView == nullptr)
        return;
    if (pitchOpen)
    {
        webView->setBounds (bodyArea);
        webView->toFront (false);
        webView->setVisible (true);
        pushState();
        setStatus ("REACT WORKSPACE / transport, arrangement, mixer and pitch");
    }
    else
    {
        webView->setVisible (false);
    }
}
} // namespace nc
