#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "AuxBus.h"
#include "MixRack.h"
#include "TransportClock.h"
#include "MidiLearnManager.h"
#include "MidiRecorder.h"
#include "PluginCatalog.h"
#include "TrackInsertRack.h"
#include "AssetLibrary.h"
#include "Automation.h"

#include <nc/engine/AudioSuite.hpp>
#include <nc/engine/Export.hpp>
#include <nc/engine/Markers.hpp>
#include <nc/engine/MidiClip.hpp>
#include <nc/engine/MidiPitch.hpp>
#include <nc/engine/MidiSchedule.hpp>
#include <nc/engine/MidiScheduler.hpp>
#include <nc/engine/PitchStack.hpp>
#include <nc/engine/ProjectLimits.hpp>
#include <nc/engine/Resample.hpp>
#include <nc/engine/SpscQueue.hpp>
#include <nc/engine/SyncClock.hpp>
#include <nc/engine/TempoMap.hpp>

#include <array>
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace nc
{
class AudioEngine final : private juce::AudioIODeviceCallback
{
public:
    // The engine allocates the full ceiling once; the session shows a subset
    // the user grows with Track > Add. Nothing resizes on the audio thread.
    static constexpr int trackCount = nc::limits::maxTracks;
    static_assert (trackCount <= nc::limits::maxTracks,
                   "track count must fit central limits");

    // Only the shipped demo record generates assets; the remaining rows are
    // empty until audio is imported, which keeps startup RAM bounded.
    static constexpr int demoTrackCount = nc::limits::defaultActiveTracks;

    /** A clip on the project timeline. Bars are 1-based; source starts at
        sourceOffsetSeconds inside the referenced asset. The audio callback
        never sees this message-thread object. */
    struct TimelineClip
    {
        int assetId = -1;
        int track = 0;
        double startBar = 1.0;
        double lengthBars = 1.0;
        double sourceOffsetSeconds = 0.0;
        float gainDb = 0.0f;
        float fadeInBars = 0.0f;
        float fadeOutBars = 0.0f;
        float crossfadeBars = 0.0f; // equal-power overlap; 0 = linear fades
    };

    AudioEngine();
    ~AudioEngine() override;

    void setPlaying (bool shouldPlay);
    bool isPlaying() const { return clock.isPlaying(); }

    void setMetronome (bool on);
    bool isMetronomeOn() const { return clock.isMetronomeOn(); }

    void setBpm (double newBpm);
    double getBpm() const { return clock.bpm(); }

    //==========================================================================
    // Tempo map + markers (Task 2) and MIDI clips (Task 4). Snapshots are
    // published lock-free: the message thread replaces the owned snapshot,
    // the audio callback only reads the live pointer. Retired snapshots are
    // freed by reclaimRetiredSnapshots() once no callback can still read them.
    //==========================================================================
    void setTempoMap (nc::engine::TempoMap map);
    nc::engine::TempoMap tempoMap() const;
    void setMarkers (std::vector<nc::engine::Marker> markers);
    const std::vector<nc::engine::Marker>& markers() const { return clock.markers(); }

    struct MidiTimelineClip
    {
        int track = 0;
        double startBar = 1.0;
        double lengthBars = 4.0;
        juce::String name;
        std::vector<nc::engine::MidiNote> notes;
        std::vector<nc::engine::MidiCC> ccs;
        std::vector<nc::engine::MidiBend> bends;
    };

    void setMidiClips (std::vector<MidiTimelineClip> clips);
    std::vector<MidiTimelineClip> midiClips() const;

    /** Imports a MIDI file (type 0/1) as one clip on `track` at `startBar`.
        Returns the clip index or -1. Message thread. */
    int importMidiFile (const juce::File& file, int track, double startBar);

    /** Exports all MIDI clips on `track` as one MIDI file. Message thread. */
    bool exportMidiFile (const juce::File& file, int track) const;

    // Track instrument hosting (Task 4). The instrument lane receives the
    // scheduled MidiBuffer; inserts process its audio afterwards.
    bool loadTrackInstrument (int track, const juce::PluginDescription&, juce::String& error);
    bool restoreTrackInstrument (int track, const nc::engine::PluginSlotState&, juce::String& error);
    void unloadTrackInstrument (int track);
    void setBuiltinInstrument (int track, int wave = 0);
    juce::String trackInstrumentName (int track) const;
    bool trackInstrumentIsMissing (int track) const;
    nc::engine::PluginSlotState trackInstrumentState (int track) const;

    /** Live hosted instance behind a slot, for editor windows. Null for
        the built-in synth, missing, or empty slots. Message thread. */
    juce::AudioPluginInstance* trackInstrumentInstance (int track) const;
    juce::AudioPluginInstance* trackInsertInstance (int track, int slot) const;
    int scannedInstrumentCount() const;
    juce::String scannedInstrumentName (int index) const;
    bool loadTrackInstrumentByIndex (int track, int scannedIndex, juce::String& error);
    bool loadBuiltinInstrumentForTrack (int track, int wave, juce::String& error);
    bool loadTrackInstrumentFromId (int track, const juce::String& catalogId, juce::String& error);

    /** Visible session rows, 1..trackCount. */
    void setActiveTrackCount (int count);
    int getActiveTrackCount() const { return activeTrackCount.load(); }

    void setTrackGain (int track, float linearGain);
    float trackGain (int track) const;
    void setTrackPan (int track, float pan);
    void setTrackSendA (int track, float send);
    float trackSendA (int track) const;
    void setTrackSendB (int track, float send);
    float trackSendB (int track) const;
    void setTrackWidth (int track, float width);
    float trackWidth (int track) const;

    //==========================================================================
    // Automation. A non-empty lane drives the fader for as long as it exists;
    // an empty lane hands the control back to the fader itself.
    //==========================================================================
    void setTrackAutomation (int track, AutomationParam param, const AutomationLane& lane);
    void clearTrackAutomation (int track, AutomationParam param);
    AutomationLane trackAutomation (int track, AutomationParam param) const;
    float automationValue (int track, AutomationParam param, double beats) const;

    /** Per-track AUTO switch. Off hands the fader back to its static value and
        keeps the lanes intact, so re-enabling restores the performance. */
    void setAutomationRead (int track, bool enabled);
    bool automationRead (int track) const;
    float trackPan (int track) const;
    void setTrackMuted (int track, bool muted);
    bool trackMuted (int track) const;
    void setTrackSolo (int track, bool soloed);
    bool trackSolo (int track) const;

    //==========================================================================
    // Pitch: global + per-track + per-instrument (ported from ChordStudio).
    // Message thread writes, audio thread reads atomics (gain/pan pattern).
    //==========================================================================
    void setGlobalPitch (const nc::engine::PitchStack& stack);
    nc::engine::PitchStack globalPitchStack() const noexcept;
    void setTrackPitch (int track, const nc::engine::PitchStack& stack);
    nc::engine::PitchStack trackPitchStack (int track) const noexcept;
    void setInstrumentPitch (int track, const nc::engine::PitchStack& stack);
    nc::engine::PitchStack instrumentPitchStack (int track) const noexcept;

    void setMasterGain (float linearGain);
    float masterGain() const { return masterGainValue.load(); }

    /** Monitor-only controls. They are applied after the master insert and
        never alter the rendered project state. */
    void setMasterDim (bool on) { masterDim.store (on); }
    bool isMasterDimOn() const { return masterDim.load(); }
    void setMasterMono (bool on) { masterMono.store (on); }
    bool isMasterMonoOn() const { return masterMono.load(); }

    float trackPeak (int track) const;
    float masterPeak() const { return masterPeakValue.load(); }
    float masterPeakLeft() const { return masterPeakLeftValue.load(); }
    float masterPeakRight() const { return masterPeakRightValue.load(); }

    double positionSeconds() const;

    /** Musical and transport context for the current position, in the shape a
        hosted VST3 plugin expects. Message thread; reads atomics only. */
    nc::engine::TransportBlock transportBlock (int blockSize = 0) const;

    //==========================================================================
    // Master insert hosting. Message thread; the callback consumes the slot.
    //==========================================================================
    int scanPlugins();
    /** Loadable insert rows: scanned effect descriptions, including registry
        rows that own one. Instruments and native rows are not insertable here. */
    int scannedPluginCount() const;
    juce::String scannedPluginName (int index) const;
    bool loadMasterPlugin (int index);
    void removeMasterPlugin();
    bool hasMasterPlugin() const;
    juce::String masterPluginName() const;

    /** Latency the insert reports, in samples. 0 when nothing is loaded. */
    int masterPluginLatency() const;

    /** State of the master insert, ready for the project file. Empty when no
        plugin is loaded. */
    juce::String masterPluginIdentifier() const;
    juce::String masterPluginStateBase64() const;

    /** Re-loads the saved insert and applies its state. False when the plugin
        is no longer installed or the state is unreadable. */
    bool restoreMasterPlugin (const juce::String& identifier, const juce::String& stateBase64);
    bool restoreMasterInsert (const nc::engine::PluginSlotState&, juce::String& error);
    bool loadMasterInsert (const juce::PluginDescription&, juce::String& error);
    void setMasterInsertBypassed (bool bypassed);
    bool masterInsertIsMissing() const;
    nc::engine::PluginSlotState masterInsertState() const;

    /** Live hosted instance for the master editor window, or null. */
    juce::AudioPluginInstance* masterInsertInstance() const;

    //==========================================================================
    // Track insert hosting. The four user slots are effects-only in Phase 2.
    //==========================================================================
    PluginCatalog& pluginCatalog() { return catalog; }
    const PluginCatalog& pluginCatalog() const { return catalog; }

    bool loadTrackInsert (int track, int slot, const juce::PluginDescription&, juce::String& error);
    bool loadTrackInsertFromId (int track, int slot, const juce::String& catalogId, juce::String& error);

    /** Installs the built-in studio-FX chain on a track insert. An empty
        chain JSON installs a passthrough. Message thread. */
    bool setBuiltinStudioFx (int track, int slot, const juce::String& chainJson, juce::String& error);
    bool restoreTrackInsert (int track, int slot, const nc::engine::PluginSlotState&, juce::String& error);
    void unloadTrackInsert (int track, int slot);

    /** Reorders a track's insert slots, keeping every plugin instance and its
        state. Returns false for an out-of-range slot or a no-op move. */
    bool moveTrackInsert (int track, int from, int to);
    void setTrackInsertBypassed (int track, int slot, bool);
    bool trackInsertIsBypassed (int track, int slot) const;
    juce::String trackInsertName (int track, int slot) const;
    bool trackInsertIsMissing (int track, int slot) const;
    nc::engine::PluginSlotState trackInsertState (int track, int slot) const;
    int trackInsertLatency (int track, int slot) const;
    void pollRetiredInserts()
    {
        inserts.collectGarbage();
        reclaimRetiredSnapshots();
    }

    /** Frees replaced tempo/MIDI/automation snapshots once no audio callback
        can still be holding a pointer to them. Message thread only. */
    void reclaimRetiredSnapshots();

    /** Diagnostics and tests: how many replaced snapshots are still retained. */
    int retainedSnapshotCount() const noexcept
    {
        return static_cast<int> (automationRetired.size() + clock.retainedTempoCount()
                                 + midiRetired.size());
    }

    std::function<void (juce::String)> onPluginStatus;

    /** Fired on the message thread once an asynchronous insert load settles. */
    std::function<void()> onInsertInstalled;

    static constexpr int kInsertSlotsPerTrack = 8;

    //==========================================================================
    // Baked mix rack. The channel strip exists on every track; the master rack
    // always terminates the mix before the optional external master insert.
    // Message thread for parameter/state access; process() stays on the audio
    // callback.
    //==========================================================================
    MixRackChannel& mixChannel (int track);
    const MixRackChannel& mixChannel (int track) const;
    MasterRack& masterMix() { return masterRack; }
    const MasterRack& masterMix() const { return masterRack; }

    juce::String mixRackStateJson() const;
    bool restoreMixRackStateJson (const juce::String& state);
    juce::String deviceDescription() const { return deviceInfo; }
    /** Device manager for the settings panel. The selector component applies
        device, rate and buffer changes live; the engine re-prepares on the
        audio-device callbacks. Message thread. */
    juce::AudioDeviceManager& audioDeviceManager() { return deviceManager; }
    MidiLearnManager& getMidiLearn() { return midiLearn; }
    const MidiLearnManager& getMidiLearn() const { return midiLearn; }
    void setMidiLearnTrack (int track) { midiLearnTrack = juce::jlimit (0, trackCount - 1, track); }
    int pollMidiLearnChanges();

    // Audio interface persistence (JUCE ApplicationProperties, XML blob).
    // Restores preferred device/rate/buffer/midi on next launch; falls back
    // to OS default if the stored device is unplugged. Message thread.
    void saveAudioSettings();
    void restoreAudioSettings();
    void clearAudioSettings();
    juce::String audioSettingsSummary() const;
    /** Live hardware state for the React settings panel: current setup, the
        measured device state, and the device names each driver offers. Scans
        each device type, so call it on the message thread only. */
    juce::var audioStateToVar();

    /** Applies a driver/device/rate/buffer choice, letting the device pick its
        default channel set (a stereo codec must never be asked for 11
        channels). Returns { ok, error, note, state }. Message thread. */
    juce::var applyAudioSettings (const juce::String& deviceType,
                                  const juce::String& outputDevice,
                                  double sampleRate, int bufferSize);

    /** Swaps which engine an aux return runs. Aux 0 is the reverb send and aux
        1 the delay send; each hosts the stock Overlay engine or its swap target
        (Nexus Reverb / Temporal Delay). Message thread. */
    void setAuxEngine (int aux, AuxBus::Engine engine);
    AuxBus::Engine auxEngine (int aux) const;
    juce::String auxEngineName (int aux) const;
    static juce::String auxEngineNameOf (AuxBus::Engine engine)
    {
        return AuxBus::engineName (engine);
    }

    // Facts the chrome reports. Nothing here is estimated.
    double currentSampleRate() const { return sampleRate.load(); }
    int currentBufferSize() const { return bufferSize.load(); }
    bool isDeviceOpen() const { return deviceOpen.load(); }
    float dspLoad() const { return dspLoadValue.load(); }
    int deviceBufferSize() const { return bufferSize.load (std::memory_order_relaxed); }
    double deviceSampleRate() const { return sampleRate.load (std::memory_order_relaxed); }
    /** Callbacks that overran the device budget — every one is an audible
        dropout. Reads alongside dspBlockCount (blocks measured since the
        device opened) so a rate can be computed. */
    uint64_t dspOverruns() const { return dspOverrunCount.load (std::memory_order_relaxed); }
    uint64_t dspBlocksMeasured() const { return dspBlockCount.load (std::memory_order_relaxed); }

    // Low latency: buffer latency + worst-track PDC + master lookahead limiter.
    // All values are measured (device getter + HostedInsert::latencySamples
    // poll), never estimated. Used by the chrome latency readout.
    double bufferLatencyMs() const;
    double worstTrackLatencyMs() const;
    double masterLatencyMs() const;
    double totalLatencyMs() { return bufferLatencyMs() + worstTrackLatencyMs() + masterLatencyMs(); }
    void setLowLatencyMode (bool shouldLow);
    bool isLowLatencyMode() const { return lowLatency.load(); }

    // Voice-to-MIDI (offline): runs YIN note tracking over an audio file and
    // returns an importable MIDI clip (monophonic, same path as MidiRecorder).
    nc::engine::ClipState voiceToMidiClip (const juce::File& file, int track, double bpm,
                                           juce::String* error = nullptr);
    void voiceToMidiClipAsync (const juce::File& file, int track, double bpm,
                               std::function<void (nc::engine::ClipState, juce::String)> completion);
    juce::String lastVoiceError() const { return voiceError; }
    int addMidiClip (nc::engine::ClipState clip);

    void resetPosition();
    void setPositionSeconds (double seconds);

    //==========================================================================
    // Project audio. These calls are message-thread only.
    //==========================================================================
    int loadAudioFile (const juce::File& file);
    juce::String lastAssetError() const { return assetError; }
    double assetDurationSeconds (int assetId) const;
    juce::String assetName (int assetId) const;

    /** Source file behind an asset; empty when the audio is generated. */
    juce::File assetFile (int assetId) const;

    /** Index of the generated demo asset for a track, or -1. */
    int demoAssetId (int track) const;

    /** Replaces the engine's entire schedule. The callback consumes the
        commands at the next block boundary. */
    /** Replaces the engine's entire schedule. Returns clips dropped as
        invalid references or beyond the 1024-clip timeline so the UI can
        report a truncated project instead of losing it silently. */
    int setTimelineClips (std::vector<TimelineClip> clips);
    juce::String lastTimelineError() const { return timelineError; }

    /** Message-thread mirror of the schedule last accepted, so a UI can project
        the timeline without touching the audio callback's queue. Written only
        by setTimelineClips; never read on the audio thread. */
    const std::vector<TimelineClip>& timelineClips() const { return timelineMirror; }

    /** Peak envelope for the part of an asset a clip actually plays, so a UI can
        draw the real waveform instead of a placeholder. Returns `bins` values in
        0..1, or empty when the audio is not resident (a streamed long import
        only keeps a read-ahead window). Message thread. */
    /** Copies the resident audio a clip plays, mixed to mono, for analysis.
        Empty for a streamed asset, whose frames are not all resident. */
    juce::AudioBuffer<float> assetSamples (int assetId, double startSeconds,
                                           double lengthSeconds) const;

    std::vector<float> assetPeakEnvelope (int assetId, double startSeconds,
                                          double lengthSeconds, int bins) const;

    /** AudioSuite: renders a processed copy of an asset into a new asset and
        returns its id, or -1. The source asset is never modified. */
    int processAsset (int sourceAssetId, const juce::String& nameSuffix,
                      const nc::engine::audiosuite::Process& process);

    /** First audible frame and length of a resident asset, for clip analysis. */
    nc::engine::audiosuite::Trim assetTrim (int assetId, float threshold = 0.003f) const;

    double assetSampleRate (int assetId) const;

    /** Refills streamed read-ahead windows. Message thread only. */
    void serviceStreaming();

    void setLoopEnabled (bool on);
    bool isLoopEnabled() const { return clock.isLoopEnabled(); }
    void setLoopBars (double startBar, double endBar);
    double getLoopStartBar() const { return clock.loopStartBar(); }
    double getLoopEndBar() const { return clock.loopEndBar(); }

    /** Last bar any timeline clip reaches, so an export can cover the session
        without guessing. Message thread. */
    double contentEndBar() const { return contentEnd.load(); }

    /** Renders the session mix between two bars (1-based) to a WAV file on this
        thread, with no audio device involved. It runs the same block path as
        playback, so the file matches what is heard; plugin delay compensation
        is pre-rolled and dropped exactly as nc::engine::planRender() describes.
        The metronome and loop are suspended for the render and restored after.
        Leaves the transport and the device callback as it found them. */
    bool renderToWav (const juce::File& destination,
                      double startBar,
                      double endBar,
                      int bitDepth,
                      juce::String& error);

    /** Variant export: WAV/FLAC/MP3/AIFF, TPDF dither on bit-depth reduction,
        optional LUFS normalisation to the target, loudness measured and
        available via lastExportLoudness(). Two-pass (measure -> gain/dither ->
        write) over an in-memory stereo buffer, capped at 30 minutes. */
    bool renderToFile (const juce::File& destination,
                       double startBar,
                       double endBar,
                       const nc::engine::export_::ExportOptions& options,
                       juce::String& error);

    /** One file per active track over the same range/rate/dither as the mix.
        Each stem solos one track (all others muted) and restores the prior
        mute/solo state afterwards. Returns files written; error on failure. */
    int exportStems (const juce::File& folder,
                     double startBar,
                     double endBar,
                     const nc::engine::export_::ExportOptions& options,
                     std::vector<juce::File>& written,
                     juce::String& error);

    nc::engine::export_::LoudnessResult lastExportLoudness() const { return lastLoudness; }

    //==========================================================================
    // Recording. Capture is input-aligned (Ardour's disk-writer rule): the
    // callback only appends to preallocated buffers, and the take is trimmed by
    // the reported input latency when it is written, never during capture.
    //==========================================================================
    void setRecordArmed (int track, bool armed);
    bool isRecordArmed (int track) const;

    /** Input monitoring: armed tracks hear their input through the insert
        chain (default on). Direct-monitor bypass is the device's own. */
    void setTrackMonitor (int track, bool on);
    bool isTrackMonitor (int track) const;

    /** Set by the audio thread when a punch-out (loop end) is crossed
        while recording; the message thread finalises the take. */
    bool consumePunchOut();

    //==========================================================================
    // MIDI recording (Task 5). Device callback -> SPSC -> quantized clip.
    //==========================================================================
    static juce::Array<juce::MidiDeviceInfo> availableMidiDevices();
    bool openMidiDevice (const juce::String& deviceIdentifier, juce::String& error);
    void closeMidiDevice();
    bool startMidiRecording (int track, juce::String& error);
    bool isMidiRecording() const { return midiRecorder.isRecording(); }
    void drainMidiRecording();
    nc::engine::ClipState stopMidiRecording();

    /** Opens a capture buffer per armed track. Fails when nothing is armed or
        the engine has no usable rate yet. */
    bool startRecording (juce::String& error);

    /** Streaming variant: takes write straight to `folder` via threaded
        writers, so long takes are disk-bound, not RAM-bound. */
    bool startRecording (const juce::File& folder, juce::String& error);
    bool isRecording() const { return recording.load(); }

    /** Frames captured since startRecording, across all armed tracks. */
    int64_t recordedFrames() const;

    /** Input latency trimmed from the head of each take, in samples. */
    void setRecordLatencySamples (int samples) { recordLatency.store (juce::jmax (0, samples)); }
    int recordLatencySamples() const { return recordLatency.load(); }

    /** Buffers that overflowed during the take (Ardour's x-run counter). */
    int recordOverflows() const;

    struct RecordedTake
    {
        int track = -1;
        juce::File file;
        int64_t startFrame = 0;   // transport frame the take starts at
        int64_t frames = 0;       // frames written, after the latency trim
    };

    /** Writes each armed track's capture to its own WAV in `folder` and returns
        the takes. Message thread; no device access. */
    std::vector<RecordedTake> stopRecording (const juce::File& folder,
                                             int bitDepth,
                                             juce::String& error);

    /** Drives one block through the same path the device callback uses, so the
        recording and mix behaviour can be tested without hardware. */
    void processBlockForTesting (const float* const* inputChannelData,
                                 int numInputChannels,
                                 float* const* outputChannelData,
                                 int numOutputChannels,
                                 int numSamples);

    /** Detaches the device callback so a test owns the render path outright.
        Real-device playback stops for this engine; capture and export still run. */
    void suspendAudioCallbackForTesting();

private:
    PluginCatalog catalog;
    TrackInsertRack inserts;
    std::shared_ptr<std::atomic<bool>> engineAlive;
    juce::AudioBuffer<float> mixBuffer;
    juce::AudioBuffer<float> instrumentBlock;
    juce::AudioBuffer<float> trackBlock;
    std::array<MixRackChannel, trackCount> mixRack;
    MasterRack masterRack;
    std::atomic<int> preparedRackCount { 0 };

    /** Re-applies the baked vocal role to every channel. Message thread only.
        When restoring, only the vocal role is retained; saved strip/master
        state is never overwritten by baked defaults. */
    void applyVocalRoles (bool restoringSavedState = false);

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    using AudioAsset = AssetLibrary::Asset;

    struct PlaybackClip
    {
        const AudioAsset* asset = nullptr;
        int track = 0;
        double startBar = 1.0;
        double lengthBars = 1.0;
        double sourceOffsetSeconds = 0.0;
        float gainDb = 0.0f;
        float fadeInBars = 0.0f;
        float fadeOutBars = 0.0f;
        float crossfadeBars = 0.0f;
    };

    enum class CommandType : juce::uint8 { clear, add };

    struct Command
    {
        CommandType type = CommandType::clear;
        PlaybackClip clip;
    };

    struct Track
    {
        std::atomic<float> gain { 0.8f };
        std::atomic<float> pan { 0.0f };
        std::atomic<float> sendA { 0.0f };
        std::atomic<float> sendB { 0.0f };
        std::atomic<float> width { 0.5f }; // 0..1, 0.5 = unchanged via mid/side x2
        std::atomic<float> peak { 0.0f };
        std::atomic<bool> muted { false };
        std::atomic<bool> soloed { false };
        // Pitch scopes (message writes, audio reads).
        std::atomic<int> trackTranspose { 0 };
        std::atomic<float> trackPitchCents { 0.0f };
        std::atomic<float> trackTuningHz { 440.0f };
        std::atomic<int> instTranspose { 0 };
        std::atomic<float> instPitchCents { 0.0f };
        std::atomic<float> instTuningHz { 440.0f };
    };

    // Render scratch for the audio callback: preallocated once with the engine
    // so the 1024-clip timeline never touches the callback stack or heap.
    struct RenderClip
    {
        const AudioAsset* asset = nullptr;
        int track = 0;
        int64_t startSample = 0;
        int64_t endSample = 0;
        double sourceOffsetSeconds = 0.0;
        double lengthSeconds = 0.0;
        double fadeInSeconds = 0.0;
        double fadeOutSeconds = 0.0;
        double crossfadeSeconds = 0.0;
        float gain = 1.0f;
    };

    static constexpr int maxClips = nc::limits::maxClips;
    static_assert (maxClips <= nc::limits::maxClips,
                   "preview clip cap must fit central limits");
    static constexpr int commandCapacity = nc::limits::commandCapacity;
    static_assert (commandCapacity >= maxClips + 16,
                   "command rail must fit clear plus a full timeline");

    /** Rebuilds the resampling kernels of every asset for the output rate.
        Message thread only; forwards to AssetLibrary. */
    void refreshResampleBanks();
    void createDemoAssets();
    bool pushCommand (const Command& command);
    void processCommands();

    /** Shared per-block state handed to each render pass. Constructed once at
        the top of renderBlock; the passes read and write it instead of the
        block's locals, so no pass needs the whole argument list. */
    struct RenderContext
    {
        const float* const* inputChannelData = nullptr;
        int numInputChannels = 0;
        float* const* outputChannelData = nullptr;
        int numOutputChannels = 0;
        int numSamples = 0;

        double rate = 48000.0;
        bool isPlaying = false;
        bool clickOn = false;
        bool anySolo = false;

        double tempo = 120.0;
        double beatSamples = 0.0;
        const engine::TempoMap* tempoMap = nullptr;

        bool loop = false;
        int64_t loopStart = 0;
        int64_t loopEnd = 0;
        int64_t blockStartSamples = 0;
        double blockStartPpq = 0.0;
        int64_t position = 0;

        float* mixLeft = nullptr;
        float* mixRight = nullptr;
        bool trackBufferReady = false;

        float* auxALeft = nullptr;
        float* auxARight = nullptr;
        float* auxBLeft = nullptr;
        float* auxBRight = nullptr;

        std::array<float, trackCount> trackLeftScratch {};
        std::array<float, trackCount> trackRightScratch {};
        std::array<float, trackCount> automatedGain {};
        std::array<float, trackCount> automatedPan {};
        std::array<float, trackCount> blockPeaks {};

        float masterPeak = 0.0f;
        float masterPeakLeft = 0.0f;
        float masterPeakRight = 0.0f;
    };

    void renderClipsPass (RenderContext& ctx);
    void renderInstrumentInsertPass (RenderContext& ctx);
    void renderStripPass (RenderContext& ctx);
    void renderMasterPass (RenderContext& ctx);

    /** The one mix block, shared by the device callback and renderToWav(). */
    void renderBlock (const float* const* inputChannelData, int numInputChannels,
                      float* const* outputChannelData, int numOutputChannels, int numSamples);
    void captureInputBlock (const float* const* inputChannelData, int numInputChannels,
                            int numSamples, int64_t position);

    /** Allocates the render scratch and prepares every rack at this rate.
        Only legal while no audio callback is attached. */
    void prepareForRender (double rate, int maxBlock);

    /** One armed track's capture: preallocated, appended without allocating. */
    struct RecordBuffer
    {
        juce::AudioBuffer<float> storage;
        int64_t written = 0;
        bool overflowed = false;

        void prepare (int64_t capacityFrames);
        void push (const float* left, const float* right, int numSamples) noexcept;
        void reset() noexcept;
    };

    /** A published lane: the audio thread reads `live`, the message thread owns
        `owned`. Replaced lanes are retired and freed once no callback can still
        be reading them, or when the device stops. */
    struct AutomationSlot
    {
        std::unique_ptr<AutomationLane> owned;
        std::atomic<const AutomationLane*> live { nullptr };
    };

    std::array<AutomationSlot, trackCount> volumeAutomation;
    std::array<AutomationSlot, trackCount> panAutomation;
    /** Defaults to true; the constructor fills it, since a value-initialised
        atomic<bool> would otherwise start at false and silently mute every
        lane in the session. */
    std::array<std::atomic<bool>, trackCount> automationReadEnabled {};
    std::vector<RetiredSnapshot<AutomationLane>> automationRetired;

    struct MidiSlot
    {
        std::unique_ptr<std::vector<MidiTimelineClip>> owned;
        std::atomic<const std::vector<MidiTimelineClip>*> live { nullptr };
    };

    MidiSlot midiSlot;
    // Audio-thread scratch for MIDI scheduling: reserved once in
    // audioDeviceAboutToStart so renderBlock never allocates.
    std::vector<nc::engine::MidiEvent> midiEventScratch;

    // One cursor per published MIDI clip, parallel to the live snapshot.
    // Reset whenever the snapshot pointer changes (setMidiClips).
    std::vector<nc::engine::MidiCursor> midiCursors;
    const std::vector<MidiTimelineClip>* midiCursorsFor = nullptr;
    std::vector<RetiredSnapshot<std::vector<MidiTimelineClip>>> midiRetired;

    std::atomic<bool> captureAvailable { false };
    std::array<RecordBuffer, trackCount> recordBuffers;
    std::array<std::atomic<bool>, trackCount> recordArmed {};

    MidiRecorder midiRecorder;
    int midiLearnTrack { 0 };
    std::vector<float> recordSilence { 16384, 0.0f }; // ThreadedWriter needs non-null channels

    // Streaming takes (ThreadedWriter): when non-empty, capture pushes
    // straight to disk instead of the RAM buffers above.
    struct ThreadedTake
    {
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        juce::File file;
        int track = -1;
        int64_t startFrame = 0;
    };
    std::vector<ThreadedTake> threadedTakes;
    juce::TimeSliceThread writerThread { "RecordWriter" };
    std::array<std::atomic<bool>, trackCount> trackMonitor {};
    std::atomic<bool> punchOutPending { false };
    std::atomic<bool> recording { false };
    std::atomic<int> recordLatency { 0 };
    std::atomic<int> overflowCount { 0 };
    std::atomic<int64_t> recordStartFrame { 0 };
    int64_t recordFramesWritten = 0;

    std::array<Track, trackCount> tracks;

    /** True while renderToWav() owns the engine: the device callback renders
        silence instead of a second, interleaved pass. */
    std::atomic<bool> offlineRender { false };
    // Signalled by the audio callback while an offline render has the engine
    // parked, so renderToFile() can wait for callbacks to stand down instead
    // of sleeping for a guessed number of block durations.
    juce::WaitableEvent offlineBlockDone { false };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<float> masterGainValue { 0.8f };
    std::atomic<int> globalTranspose { 0 };
    std::atomic<float> globalPitchCents { 0.0f };
    std::atomic<float> globalTuningHz { 440.0f };
    std::atomic<bool> masterDim { false };
    std::atomic<bool> masterMono { false };
    std::atomic<float> masterPeakValue { 0.0f };
    std::atomic<float> masterPeakLeftValue { 0.0f };
    std::atomic<float> masterPeakRightValue { 0.0f };
    std::atomic<int> activeTrackCount { nc::limits::defaultActiveTracks };
    std::atomic<int> bufferSize { 0 };
    std::atomic<bool> deviceOpen { false };
    std::atomic<float> dspLoadValue { 0.0f };
    // Callbacks that ran longer than the device budget (elapsed > blockSize /
    // rate). Every one is a guaranteed audible dropout: the device had already
    // been fed nothing and interpolates or repeats the previous block.
    std::atomic<uint64_t> dspOverrunCount { 0 };
    std::atomic<uint64_t> dspBlockCount { 0 };
    std::atomic<bool> lowLatency { false };
    std::atomic<int> previousBufferSize { 0 };
    std::atomic<double> previousSampleRate { 0.0 };
    mutable juce::String voiceError;
    juce::String timelineError;
    juce::String assetError;

    void setPluginStatus (juce::String message);
    std::vector<CatalogEntry> loadablePluginEntries() const;
    std::vector<CatalogEntry> loadableInstrumentEntries() const;

    std::atomic<double> contentEnd { 1.0 };
    std::vector<TimelineClip> timelineMirror;
    nc::engine::export_::LoudnessResult lastLoudness;

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formats;
    juce::String deviceInfo;
    MidiLearnManager midiLearn;

    AssetLibrary assetLibrary;
    TransportClock clock;

    // SPSC command rail: message thread writes, audio callback drains.
    // Shared nc::engine::SpscQueue (Task 1), not an ad-hoc array.
    nc::engine::SpscQueue<Command> commandQueue { commandCapacity };

    // Audio-thread only. renderScratch is reused every block: the prep loop
    // invalidates each slot it skips so stale clips can never replay.
    std::array<PlaybackClip, maxClips> playbackClips {};
    std::array<RenderClip, maxClips> renderScratch {};
    int playbackClipCount = 0;
    double clickPhase = 0.0;
    double clickEnvelope = 0.0;
    double clickOscillator = 0.0;
    double probePhase = 0.0;   // diagnostic sine phase (NC_TEST_TONE_HZ)
    int clickBeat = 0;
    float smoothedGain[trackCount] {};
    float smoothedPan[trackCount] {};
    float smoothedSendA[trackCount] {};
    float smoothedSendB[trackCount] {};
    float smoothedWidth[trackCount] {};
    // Equal-power pan gains, recomputed only when smoothedPan moves (audio
    // thread only; NaN-free defaults so the first block is correct either way).
    float cachedPanValue[trackCount] { -2.0f };
    float cachedPanLeft[trackCount] { 0.7071f };
    float cachedPanRight[trackCount] { 0.7071f };
    float smoothedMaster = 0.8f;
    bool smoothingPrimed = false;
    bool transportWasPlaying = false;

    // Aux returns: send sums are accumulated here per block, processed by
    // AuxBus (tail state), then added to the master mix. Sized in prepare().
    juce::AudioBuffer<float> auxABuffer;
    juce::AudioBuffer<float> auxBBuffer;
    AuxBus auxReverb;
    AuxBus auxDelay;
    std::atomic<int> auxReverbEngine { static_cast<int> (AuxBus::Engine::overlayReverb) };
    std::atomic<int> auxDelayEngine { static_cast<int> (AuxBus::Engine::overlayDelay) };

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};
} // namespace nc
