#pragma once

#include <nc/engine/MidiClip.hpp>
#include <nc/engine/ProjectLimits.hpp>

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
    instrument slots and MIDI clips (ClipState::isMidi + notes). v2 files
    have no "kind"/"notes" fields and load as audio clips with no
    instruments, which is exactly the v2 behaviour. */
inline constexpr int projectSchemaVersion = 3;

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
    bool isMidi = false;     // true: notes below, no audio backing
    std::vector<MidiNote> notes; // clip-relative beats; MIDI clips only
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

struct TrackState
{
    std::string name;
    double gainDb = 0.0;
    double pan = 0.0;
    bool muted = false;
    bool soloed = false;

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
    if (! std::isfinite (document.bpm))
        document.bpm = 120.0;

    document.bpm = nc::limits::clampBpm (document.bpm);
    document.activeTrackCount = nc::limits::clampActiveTracks (document.activeTrackCount);

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
        clip.sourceOffsetSeconds = std::isfinite (clip.sourceOffsetSeconds)
                                       ? std::max (0.0, clip.sourceOffsetSeconds) : 0.0;

        if (clip.isMidi)
        {
            clip.assetPath.clear();
            sanitizeNotes (clip.notes, clipLengthBeats (clip.lengthBars));

            while (totalMidiNotes + static_cast<int64_t> (clip.notes.size()) > nc::limits::maxMidiNotesTotal
                   && ! clip.notes.empty())
                clip.notes.pop_back();

            totalMidiNotes += static_cast<int64_t> (clip.notes.size());
        }
        else
        {
            clip.notes.clear();
        }

        kept.push_back (std::move (clip));

        if (static_cast<int> (kept.size()) >= nc::limits::maxClips)
            break;
    }

    document.clips = std::move (kept);

    for (auto& track : document.tracks)
    {
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
