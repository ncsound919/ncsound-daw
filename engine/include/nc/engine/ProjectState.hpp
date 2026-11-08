#pragma once

#include <nc/engine/Comping.hpp>
#include <nc/engine/Markers.hpp>
#include <nc/engine/MidiClip.hpp>
#include <nc/engine/PitchStack.hpp>
#include <nc/engine/ProjectLimits.hpp>
#include <nc/engine/TempoMap.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace nc::engine
{
/** Bumped whenever the saved shape changes. A loader that meets a higher
    number refuses the file instead of half-reading it. v3 adds per-track
    instrument slots and MIDI clips (ClipState::isMidi + notes). v4 adds
    the tempo map and user markers; v3 and earlier files have no
    "tempoMap"/"markers" fields and load as a constant map from `bpm`
    with no markers, which is exactly the v3 behaviour. v5 adds MIDI
    controller/pitch-bend events ("ccs"/"bends" on MIDI clips); older
    files load with empty lists. v6 adds pitch ("pitch" global + "pitch" /
    "instrumentPitch" per track); older files load with defaults (0/0/440).
    v7 adds per-track sends + width ("sendA"/"sendB"/"width"); older files
    load with 0/0/0.5 (no reverb/delay send, neutral width). v8 adds
    document-level automation lanes ("automation"); older files load with
    no lanes (faders stay manual). v9 adds per-clip crossfade + comp take
    ("crossfadeBars"/"compTake"); older files load with 0/0 (linear fades,
    first take). */
inline constexpr int projectSchemaVersion = 9;

/** Clip/track gain and pan are user values, not free-form numbers: a hostile
    file must not hand the mixer a NaN or a 1e9 dB boost. */
inline constexpr double minGainDb = -60.0;
inline constexpr double maxGainDb = 60.0;

/** Per-slot cap for opaque plugin data. A hostile or truncated project must not
    make the loader hold an unbounded XML/state string. */
inline constexpr std::size_t maxPluginBlobBytes = 2u * 1024u * 1024u;

struct ClipState
{
    std::string assetPath;   // resolved relative to the project folder; empty for MIDI
    std::string name;
    int track = 0;
    double startBar = 1.0;
    double lengthBars = 1.0;
    double sourceOffsetSeconds = 0.0;
    double gainDb = 0.0;
    double fadeInBars = 0.0;
    double fadeOutBars = 0.0;
    double crossfadeBars = 0.0; // v9: equal-power overlap; 0 = linear fades
    int compTake = 0;           // v9: active comp take (0 = first take)
    bool isMidi = false;     // true: notes below, no audio backing
    std::vector<MidiNote> notes; // clip-relative beats; MIDI clips only
    std::vector<MidiCC> ccs;     // controller events; MIDI clips only
    std::vector<MidiBend> bends; // pitch-bend events; MIDI clips only
};

struct PluginSlotState
{
    std::string identifier;   // PluginDescription::createIdentifierString()
    std::string descriptionXml; // full PluginDescription XML; primary restore path
    std::string format;
    std::string stateBase64;
    bool bypassed = false;

    bool isEmpty() const noexcept { return identifier.empty() && descriptionXml.empty(); }
};

struct AutomationPointState
{
    double beats = 0.0;
    double value = 0.0;
    std::string curve = "linear"; // step | linear | smooth (v8)
};

struct AutomationLaneState
{
    int track = 0;
    std::string param = "volume"; // volume | pan (v8)
    std::vector<AutomationPointState> points;
};


struct TrackState
{
    std::string name;
    double gainDb = 0.0;
    double pan = 0.0;
    bool muted = false;
    bool soloed = false;
    double sendA = 0.0;  // 0..1, reverb send (v7)
    double sendB = 0.0;  // 0..1, delay send (v7)
    double width = 0.5;  // 0..1, stereo imager (0.5 = unchanged) (v7)

    // Pitch scopes (v6, ported from ChordStudio PitchStack).
    PitchStack pitch;
    PitchStack instrumentPitch;

    // Four user insert slots. Slot 0 in the UI is the baked native strip; these
    // are the four slots the browser can load into.
    std::array<PluginSlotState, 4> inserts {};

    // The track's instrument: MIDI in, stereo out, playing into slot 0 of the
    // insert chain. Built-in synth is identified by identifier
    // "builtin-polysynth" so a project reopens silently without a scan.
    PluginSlotState instrument;
};

/** The serialisable session. Everything a project needs to reopen exactly as
    it was left; the audio itself stays on disk behind ClipState::assetPath. */
struct ProjectDocument
{
    int version = projectSchemaVersion;
    double bpm = 120.0;
    // v4: piecewise tempo. Empty means "constant at bpm" (pre-v4 files and
    // the default document); sanitize fills it from bpm so playback code
    // can always walk the map. bpm stays as the legacy/display value and
    // is kept in sync with the map's first point on save.
    TempoMap tempoMap;
    std::vector<Marker> markers;
    PitchStack globalPitch; // v6
    int activeTrackCount = nc::limits::defaultActiveTracks;
    bool loopEnabled = false;
    double loopStartBar = 1.0;
    double loopEndBar = 5.0;
    PluginSlotState masterInsert;

    // The baked channel strips and master rack, opaque to the engine's project
    // model. Stored as JSON so a project reopens with the mix it was saved with.
    std::string mixRack;

    std::vector<TrackState> tracks;
    std::vector<ClipState> clips;
    std::vector<AutomationLaneState> automation; // v8; empty for older files
};

/** A project written by a newer build cannot be trusted to load. */
inline bool isLoadable (const ProjectDocument& document) noexcept
{
    return document.version >= 1 && document.version <= projectSchemaVersion;
}

namespace detail
{
// CWE-22: a hostile project file must not reach files outside the project
// folder via assetPath. Only clean relative paths are accepted.
inline bool isSafeRelativeAssetPath (const std::string& path) noexcept
{
    if (path.empty())
        return false;

    for (const char c : path)
        if (c == '\0')
            return false;

    // Absolute: drive letter (C:), rooted (/ or \), UNC (\\).
    if (path.size() >= 2 && path[1] == ':'
        && (std::isalpha (static_cast<unsigned char> (path[0])) != 0))
        return false;

    if (path.front() == '/' || path.front() == '\\')
        return false;

    // Any ".." segment (split on / and \) can escape the folder.
    size_t start = 0;
    while (start <= path.size())
    {
        const auto slash = path.find_first_of ("/\\", start);
        const auto length = (slash == std::string::npos) ? path.size() - start : slash - start;

        if (length == 2 && path[start] == '.' && path[start + 1] == '.')
            return false;

        if (slash == std::string::npos)
            break;

        start = slash + 1;
    }

    return true;
}
} // namespace detail

/** Brings a loaded document back inside every central limit. Values the engine
    would refuse to run are dropped or clamped here rather than at playback, so
    a hand-edited or truncated file cannot reach the audio thread malformed.
    Idempotent: sanitising twice equals sanitising once. */
inline ProjectDocument sanitize (ProjectDocument document)
{
    // An older file that now carries the current shape is upgraded when it is
    // re-saved. A newer schema is left untouched so isLoadable() still refuses
    // it instead of silently downgrading a document this build cannot read.
    if (document.version < projectSchemaVersion)
        document.version = projectSchemaVersion;

    document.globalPitch.clamp();

    if (! std::isfinite (document.bpm))
        document.bpm = 120.0;

    document.bpm = nc::limits::clampBpm (document.bpm);
    document.activeTrackCount = nc::limits::clampActiveTracks (document.activeTrackCount);

    if (document.tempoMap.empty())
        document.tempoMap = constantTempoMap (document.bpm);
    else
    {
        sanitizeTempoMap (document.tempoMap);
        document.bpm = document.tempoMap.points.front().bpm;
    }

    sanitizeMarkers (document.markers);

    // Rack state is a fixed-size snapshot; a corrupt or hostile file cannot
    // make the project loader hold an unbounded string.
    if (document.mixRack.size() > 2u * 1024u * 1024u)
        document.mixRack.clear();

    const auto maxBar = static_cast<double> (nc::limits::maxBars);

    if (! std::isfinite (document.loopStartBar))
        document.loopStartBar = 1.0;

    if (! std::isfinite (document.loopEndBar))
        document.loopEndBar = document.loopStartBar + 4.0;

    document.loopStartBar = std::clamp (document.loopStartBar, 1.0, maxBar);
    document.loopEndBar = std::clamp (document.loopEndBar, document.loopStartBar + 1.0, maxBar + 1.0);

    if (static_cast<int> (document.tracks.size()) > nc::limits::maxTracks)
        document.tracks.resize (static_cast<size_t> (nc::limits::maxTracks));

    const auto lastTrack = std::max (0, document.activeTrackCount - 1);
    std::vector<ClipState> kept;
    kept.reserve (std::min (document.clips.size(), static_cast<size_t> (nc::limits::maxClips)));

    int64_t totalMidiNotes = 0;

    for (auto clip : document.clips)
    {
        if (! clip.isMidi && clip.assetPath.empty())
            continue;

        if (! clip.isMidi && ! detail::isSafeRelativeAssetPath (clip.assetPath))
            continue;

        if (! std::isfinite (clip.startBar) || ! std::isfinite (clip.lengthBars))
            continue;

        if (clip.lengthBars <= 0.0)
            continue;

        clip.track = std::clamp (clip.track, 0, lastTrack);
        clip.startBar = std::clamp (clip.startBar, 1.0, maxBar);
        clip.lengthBars = std::clamp (clip.lengthBars, 1.0e-6, maxBar);
        clip.crossfadeBars = std::isfinite (clip.crossfadeBars)
                                 ? std::clamp (clip.crossfadeBars, 0.0, clip.lengthBars * 0.45)
                                 : 0.0;
        clip.compTake = std::clamp (clip.compTake, 0,
                                    static_cast<int> (comping::CompLane::maxTakes) - 1);
        clip.sourceOffsetSeconds = std::isfinite (clip.sourceOffsetSeconds)
                                       ? std::max (0.0, clip.sourceOffsetSeconds) : 0.0;
        clip.gainDb = std::isfinite (clip.gainDb) ? std::clamp (clip.gainDb, minGainDb, maxGainDb) : 0.0;
        clip.fadeInBars = std::isfinite (clip.fadeInBars)
                              ? std::clamp (clip.fadeInBars, 0.0, clip.lengthBars * 0.45) : 0.0;
        clip.fadeOutBars = std::isfinite (clip.fadeOutBars)
                               ? std::clamp (clip.fadeOutBars, 0.0, clip.lengthBars * 0.45) : 0.0;

        if (clip.isMidi)
        {
            clip.assetPath.clear();
            sanitizeNotes (clip.notes, clipLengthBeats (clip.lengthBars));
            sanitizeMidiCCs (clip.ccs, clipLengthBeats (clip.lengthBars));
            sanitizeMidiBends (clip.bends, clipLengthBeats (clip.lengthBars));

            while (totalMidiNotes + static_cast<int64_t> (clip.notes.size()) > nc::limits::maxMidiNotesTotal
                   && ! clip.notes.empty())
                clip.notes.pop_back();

            totalMidiNotes += static_cast<int64_t> (clip.notes.size());
        }
        else
        {
            clip.notes.clear();
            clip.ccs.clear();
            clip.bends.clear();
        }

        kept.push_back (std::move (clip));

        if (static_cast<int> (kept.size()) >= nc::limits::maxClips)
            break;
    }

    document.clips = std::move (kept);

    {
        std::vector<AutomationLaneState> lanes;
        lanes.reserve (document.automation.size());

        for (auto& lane : document.automation)
        {
            if (lane.track < 0 || lane.track >= nc::limits::maxTracks)
                continue;

            if (lane.param != "volume" && lane.param != "pan")
                continue;

            std::vector<AutomationPointState> points;
            points.reserve (lane.points.size());

            for (auto& point : lane.points)
            {
                if (! std::isfinite (point.beats) || point.beats < 0.0)
                    continue;

                if (! std::isfinite (point.value))
                    continue;

                point.beats = std::clamp (point.beats, 0.0, static_cast<double> (nc::limits::maxBars) * 4.0);

                if (point.curve != "step" && point.curve != "linear" && point.curve != "smooth")
                    point.curve = "linear";

                points.push_back (point);

                if (static_cast<int> (points.size()) >= 512)
                    break;
            }

            if (points.empty())
                continue;

            lane.points = std::move (points);
            lanes.push_back (std::move (lane));

            if (static_cast<int> (lanes.size()) >= nc::limits::maxTracks * 2)
                break;
        }

        document.automation = std::move (lanes);
    }

    for (auto& track : document.tracks)
    {
        track.pitch.clamp();
        track.instrumentPitch.clamp();
        track.gainDb = std::isfinite (track.gainDb) ? std::clamp (track.gainDb, minGainDb, maxGainDb) : 0.0;
        track.pan = std::isfinite (track.pan) ? std::clamp (track.pan, -1.0, 1.0) : 0.0;
        track.sendA = std::isfinite (track.sendA) ? std::clamp (track.sendA, 0.0, 1.0) : 0.0;
        track.sendB = std::isfinite (track.sendB) ? std::clamp (track.sendB, 0.0, 1.0) : 0.0;
        track.width = std::isfinite (track.width) ? std::clamp (track.width, 0.0, 1.0) : 0.5;
        for (auto& slot : track.inserts)
        {
            if (slot.descriptionXml.size() > maxPluginBlobBytes)
                slot.descriptionXml.clear();

            if (slot.stateBase64.size() > maxPluginBlobBytes)
                slot.stateBase64.clear();

            if (slot.isEmpty())
                slot = {};
        }

        if (track.instrument.descriptionXml.size() > maxPluginBlobBytes)
            track.instrument.descriptionXml.clear();

        if (track.instrument.stateBase64.size() > maxPluginBlobBytes)
            track.instrument.stateBase64.clear();

        if (track.instrument.isEmpty())
            track.instrument = {};
    }

    return document;
}
} // namespace nc::engine
