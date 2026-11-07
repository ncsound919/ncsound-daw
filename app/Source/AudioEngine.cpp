#include "AudioEngine.h"

#include "Session.h"

#include <nc/engine/ClipTimeline.hpp>
#include <nc/engine/ProjectLimits.hpp>
#include <nc/engine/StreamWindow.hpp>

#include <algorithm>
#include <cmath>

namespace nc
{
namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;
constexpr float smoothingCoefficient = 0.0025f;

float softLimit (float value)
{
    return std::tanh (value);
}
} // namespace

AudioEngine::AudioEngine()
{
    engineAlive = std::make_shared<std::atomic<bool>> (true);
    formats.registerBasicFormats();

    // Ask for capture channels so recording has something to record, but never
    // lose playback on a machine without an input: fall back to output-only.
    auto error = deviceManager.initialiseWithDefaultDevices (2, 2);

    if (error.isNotEmpty())
        error = deviceManager.initialiseWithDefaultDevices (0, 2);

    deviceInfo = error.isNotEmpty() ? "no audio device" : deviceManager.getCurrentAudioDeviceType();
    deviceManager.addAudioCallback (this);

    // The shipped project is a demo record. These assets make Play audible and
    // exercise the same path as imported files; File > Import Audio replaces
    // them with real recordings.
    createDemoAssets();

    // Vocal tracks carry the Open Mic stage. The role comes from the session
    // model, not from a plugin name, so a renamed vocal track still gets it.
    applyVocalRoles();
}

AudioEngine::~AudioEngine()
{
    engineAlive->store (false, std::memory_order_release);
    deviceManager.removeAudioCallback (this);
    inserts.collectGarbage();
}

void AudioEngine::setPlaying (bool shouldPlay)    { playing.store (shouldPlay); }
void AudioEngine::setMetronome (bool on)          { metronome.store (on); }

void AudioEngine::setBpm (double newBpm)
{
    bpm.store (nc::limits::clampBpm (newBpm));
}

void AudioEngine::setActiveTrackCount (int count)
{
    const auto clamped = nc::limits::clampActiveTracks (count);
    const auto prepared = preparedRackCount.load();

    // Growing the session must not leave a visible track with an unprepared
    // strip. This is the message thread, so allocating here is safe.
    if (deviceOpen.load() && clamped > prepared)
    {
        for (int i = prepared; i < clamped; ++i)
            mixRack[static_cast<size_t> (i)].prepare (sampleRate.load(), juce::jmax (1, bufferSize.load()));

        preparedRackCount.store (clamped);
    }

    inserts.setActiveTrackCount (clamped);
    activeTrackCount.store (clamped);
}

void AudioEngine::setTrackGain (int track, float linearGain)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].gain.store (juce::jlimit (0.0f, 2.0f, linearGain));
}

float AudioEngine::trackGain (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) ? tracks[static_cast<size_t> (track)].gain.load() : 0.0f;
}

void AudioEngine::setTrackPan (int track, float pan)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].pan.store (juce::jlimit (-1.0f, 1.0f, pan));
}

float AudioEngine::trackPan (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) ? tracks[static_cast<size_t> (track)].pan.load() : 0.0f;
}

void AudioEngine::setTrackMuted (int track, bool muted)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].muted.store (muted);
}

bool AudioEngine::trackMuted (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) && tracks[static_cast<size_t> (track)].muted.load();
}

void AudioEngine::setTrackSolo (int track, bool soloed)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].soloed.store (soloed);
}

bool AudioEngine::trackSolo (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) && tracks[static_cast<size_t> (track)].soloed.load();
}

void AudioEngine::setMasterGain (float linearGain) { masterGainValue.store (juce::jlimit (0.0f, 2.0f, linearGain)); }

float AudioEngine::trackPeak (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) ? tracks[static_cast<size_t> (track)].peak.load() : 0.0f;
}

int AudioEngine::scanPlugins()
{
    return catalog.scanSync();
}

std::vector<CatalogEntry> AudioEngine::loadablePluginEntries() const
{
    std::vector<CatalogEntry> result;

    for (const auto& entry : catalog.snapshot())
        if (entry.external && entry.scanned)
            result.push_back (entry);

    return result;
}

int AudioEngine::scannedPluginCount() const
{
    return static_cast<int> (loadablePluginEntries().size());
}

juce::String AudioEngine::scannedPluginName (int index) const
{
    const auto entries = loadablePluginEntries();

    if (! juce::isPositiveAndBelow (index, static_cast<int> (entries.size())))
        return {};

    const auto& entry = entries[static_cast<size_t> (index)];
    return entry.manufacturer.isEmpty() ? entry.name : entry.name + " / " + entry.manufacturer;
}

bool AudioEngine::loadMasterPlugin (int index)
{
    const auto entries = loadablePluginEntries();

    if (! juce::isPositiveAndBelow (index, static_cast<int> (entries.size())))
        return false;

    const auto& entry = entries[static_cast<size_t> (index)];

    juce::String error;
    return pluginHost.loadDescription (entry.description, juce::jmax (1.0, sampleRate.load()),
                                       juce::jmax (1, bufferSize.load()), error);
}

void AudioEngine::removeMasterPlugin()
{
    pluginHost.releasePlugin();
}

bool AudioEngine::hasMasterPlugin() const
{
    return pluginHost.hasPlugin();
}

juce::String AudioEngine::masterPluginName() const
{
    return pluginHost.pluginName();
}

int AudioEngine::masterPluginLatency() const
{
    return pluginHost.latencySamples() + masterRack.latencySamples();
}

nc::engine::TransportBlock AudioEngine::transportBlock (int blockSize) const
{
    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto tempo = juce::jmax (1.0, bpm.load());
    const auto loopStart = static_cast<int64_t> (std::llround (engine::barToSeconds (loopStartBar.load(), tempo) * rate));
    const auto loopEnd = static_cast<int64_t> (std::llround (engine::barToSeconds (loopEndBar.load(), tempo) * rate));

    return engine::blockContext (positionSamples.load(), blockSize, tempo, rate,
                                 playing.load(), loopEnabled.load(), loopStart, loopEnd);
}

double AudioEngine::positionSeconds() const
{
    const auto rate = sampleRate.load();
    return rate > 0.0 ? static_cast<double> (positionSamples.load()) / rate : 0.0;
}

void AudioEngine::resetPosition()
{
    // The callback owns the click state; never write it from the message thread.
    resetRequested.store (true);
}

void AudioEngine::setPositionSeconds (double seconds)
{
    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto samples = static_cast<int64_t> (juce::jmax (0.0, seconds) * rate);
    positionSamples.store (samples);
    seekRequested.store (true);
}

void AudioEngine::setLoopEnabled (bool on)
{
    loopEnabled.store (on);
}

void AudioEngine::setLoopBars (double startBar, double endBar)
{
    const auto start = juce::jlimit (1.0, static_cast<double> (nc::limits::maxBars), startBar);
    const auto end = juce::jlimit (start + 1.0, static_cast<double> (nc::limits::maxBars + 1), endBar);
    loopStartBar.store (start);
    loopEndBar.store (end);
}

//==============================================================================
int AudioEngine::loadAudioFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return -1;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        return -1;

    // A bounded message-thread load against the central 30-minute ceiling at
    // the file's own rate. Full disk streaming replaces this RAM hold when
    // long projects matter; the cap stays explicit either way.
    const auto fileRate = reader->sampleRate > 0.0 ? reader->sampleRate : 48000.0;
    const auto sourceFrames = reader->lengthInSamples;

    auto asset = std::make_unique<AudioAsset>();
    asset->sampleRate = fileRate;
    asset->name = file.getFileNameWithoutExtension();
    asset->file = file;

    // Long takes stream: only the read-ahead window is resident, so a long
    // import costs seconds of RAM instead of hundreds of megabytes.
    if (static_cast<double> (sourceFrames) / fileRate > nc::limits::streamThresholdSeconds)
    {
        asset->streamed = true;
        asset->totalFrames = sourceFrames;
        asset->cacheFrames = juce::jmax<int64_t> (1024, static_cast<int64_t> (fileRate * nc::limits::streamReadAheadSeconds));

        for (auto& buffer : asset->cacheA)
            buffer.assign (static_cast<size_t> (asset->cacheFrames), 0.0f);

        for (auto& buffer : asset->cacheB)
            buffer.assign (static_cast<size_t> (asset->cacheFrames), 0.0f);

        asset->reader = std::move (reader);
        return addAsset (std::move (asset));
    }

    const auto frames64 = nc::limits::clampAssetFrames (sourceFrames, fileRate);
    const auto frames = static_cast<int> (frames64);
    const auto channels = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));

    asset->buffer.setSize (channels, frames);

    if (! reader->read (&asset->buffer, 0, frames, 0, true, true))
        return -1;

    return addAsset (std::move (asset));
}

double AudioEngine::assetDurationSeconds (int assetId) const
{
    if (assetId < 0 || assetId >= static_cast<int> (assets.size()))
        return 0.0;

    const auto& asset = *assets[static_cast<size_t> (assetId)];
    return asset.sampleRate > 0.0
               ? static_cast<double> (assetFrameCount (asset)) / asset.sampleRate
               : 0.0;
}

juce::File AudioEngine::assetFile (int assetId) const
{
    return assetId >= 0 && assetId < static_cast<int> (assets.size())
               ? assets[static_cast<size_t> (assetId)]->file
               : juce::File();
}

juce::String AudioEngine::assetName (int assetId) const
{
    return assetId >= 0 && assetId < static_cast<int> (assets.size())
               ? assets[static_cast<size_t> (assetId)]->name
               : juce::String();
}

int AudioEngine::demoAssetId (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount)
               ? demoAssets[static_cast<size_t> (track)]
               : -1;
}

int AudioEngine::addAsset (std::unique_ptr<AudioAsset> asset)
{
    if (asset == nullptr || assetFrameCount (*asset) <= 0)
        return -1;

    assets.push_back (std::move (asset));
    return static_cast<int> (assets.size()) - 1;
}

int64_t AudioEngine::assetFrameCount (const AudioAsset& asset) const
{
    return asset.streamed ? asset.totalFrames
                          : static_cast<int64_t> (asset.buffer.getNumSamples());
}

double AudioEngine::assetSampleRate (int assetId) const
{
    return assetId >= 0 && assetId < static_cast<int> (assets.size())
               ? assets[static_cast<size_t> (assetId)]->sampleRate
               : 0.0;
}

nc::engine::audiosuite::Trim AudioEngine::assetTrim (int assetId, float threshold) const
{
    if (assetId < 0 || assetId >= static_cast<int> (assets.size()))
        return {};

    const auto& asset = *assets[static_cast<size_t> (assetId)];

    if (asset.streamed || asset.buffer.getNumSamples() == 0)
        return {};

    const auto* left = asset.buffer.getReadPointer (0);
    const auto* right = asset.buffer.getNumChannels() > 1 ? asset.buffer.getReadPointer (1) : left;

    return nc::engine::audiosuite::scanSilence (left, right, asset.buffer.getNumSamples(), threshold);
}

int AudioEngine::processAsset (int sourceAssetId, const juce::String& nameSuffix,
                               const nc::engine::audiosuite::Process& process)
{
    if (sourceAssetId < 0 || sourceAssetId >= static_cast<int> (assets.size()))
        return -1;

    const auto& source = *assets[static_cast<size_t> (sourceAssetId)];

    auto rendered = std::make_unique<AudioAsset>();
    rendered->sampleRate = source.sampleRate;
    rendered->name = source.name + " " + nameSuffix;

    if (source.streamed)
    {
        const auto frames = static_cast<int> (juce::jmin<int64_t> (source.totalFrames, 3600LL * 48000LL));

        if (frames <= 0 || source.reader == nullptr)
            return -1;

        rendered->buffer.setSize (2, frames);
        rendered->buffer.clear();

        const auto hasRight = source.reader->numChannels > 1;
        source.reader->read (&rendered->buffer, 0, 0, frames, true, hasRight);

        if (! hasRight)
            rendered->buffer.copyFrom (1, 0, rendered->buffer, 0, 0, frames);
    }
    else
    {
        const auto frames = source.buffer.getNumSamples();
        const auto channels = source.buffer.getNumChannels();

        if (frames <= 0 || channels <= 0)
            return -1;

        rendered->buffer.setSize (2, frames);
        rendered->buffer.copyFrom (0, 0, source.buffer, 0, 0, frames);
        rendered->buffer.copyFrom (1, 0, source.buffer, juce::jmin (1, channels - 1), 0, frames);
    }

    const nc::engine::audiosuite::RenderView view {
        rendered->buffer.getWritePointer (0),
        rendered->buffer.getWritePointer (1),
        rendered->buffer.getNumSamples(),
        rendered->sampleRate
    };

    nc::engine::audiosuite::runProcess (view, process);
    return addAsset (std::move (rendered));
}

float AudioEngine::sampleAt (const AudioAsset& asset, int64_t frame, int channel) const
{
    if (! asset.streamed)
    {
        const auto channels = asset.buffer.getNumChannels();

        if (channels <= 0 || frame < 0 || frame >= asset.buffer.getNumSamples())
            return 0.0f;

        return asset.buffer.getReadPointer (juce::jlimit (0, channels - 1, channel))[frame];
    }

    const auto slot = static_cast<size_t> (asset.activeCache.load (std::memory_order_acquire));
    const auto start = asset.slotStart[slot].load (std::memory_order_acquire);
    const auto length = asset.slotLength[slot].load (std::memory_order_acquire);

    if (frame < start || frame >= start + length)
        return 0.0f; // not resident yet: silence, never a disk read here

    const auto& data = channel == 0 ? asset.cacheA[slot] : asset.cacheB[slot];
    const auto index = static_cast<size_t> (frame - start);

    return index < data.size() ? data[index] : 0.0f;
}

void AudioEngine::serviceStreaming()
{
    const auto seconds = positionSeconds();

    for (auto& assetPtr : assets)
    {
        auto& asset = *assetPtr;

        if (! asset.streamed || asset.reader == nullptr || asset.cacheFrames <= 0)
            continue;

        const auto cursor = static_cast<int64_t> (seconds * asset.sampleRate);
        const auto active = asset.activeCache.load (std::memory_order_acquire);
        const auto start = asset.slotStart[static_cast<size_t> (active)].load (std::memory_order_acquire);
        const auto length = asset.slotLength[static_cast<size_t> (active)].load (std::memory_order_acquire);

        // Refill once the cursor has eaten more than half the resident window.
        if (engine::windowCovers ({ start, length }, cursor, asset.cacheFrames / 2))
            continue;

        const auto target = engine::planWindow (cursor, asset.totalFrames, asset.cacheFrames);
        const auto slot = static_cast<size_t> (1 - active);
        auto& left = asset.cacheA[slot];
        auto& right = asset.cacheB[slot];
        const auto count = static_cast<int> (juce::jmin<int64_t> (target.lengthFrames, asset.cacheFrames));

        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);

        if (count > 0)
        {
            // A non-owning view over the resident window: JUCE fills it from
            // the reader without touching the buffer the callback is using.
            float* channels[2] { left.data(), right.data() };
            juce::AudioBuffer<float> view (channels, 2, count);
            const auto hasRight = asset.reader->numChannels > 1;

            asset.reader->read (&view, 0, target.startFrame, count, true, hasRight);

            if (! hasRight)
                std::copy (left.begin(), left.begin() + count, right.begin());
        }

        asset.slotStart[slot].store (target.startFrame, std::memory_order_release);
        asset.slotLength[slot].store (count, std::memory_order_release);
        asset.activeCache.store (static_cast<int> (slot), std::memory_order_release);
    }
}

void AudioEngine::createDemoAssets()
{
    constexpr double demoRate = 24000.0;
    constexpr double secondsPerBar = 2.0; // 120 BPM, 4/4
    constexpr int demoBars = 16;
    constexpr int frames = static_cast<int> (demoRate * secondsPerBar * demoBars);

    const char* const names[demoTrackCount] {
        "DEMO DRUMS", "DEMO BASS", "DEMO KEYS", "DEMO VOX",
        "DEMO GTR", "DEMO PERC", "DEMO VERB", "DEMO DELAY",
        "DEMO STRINGS", "DEMO PAD", "DEMO LEAD", "DEMO FX1",
        "DEMO FX2", "DEMO BUS", "DEMO AUX3", "DEMO AUX4"
    };

    for (int track = 0; track < demoTrackCount; ++track)
    {
        auto asset = std::make_unique<AudioAsset>();
        asset->sampleRate = demoRate;
        asset->name = names[track];
        asset->buffer.setSize (1, frames);

        auto* data = asset->buffer.getWritePointer (0);
        juce::Random random (0x4e430000 + track * 7919);
        auto noise = [&random] { return random.nextFloat() * 2.0f - 1.0f; };
        auto env = [] (double t, double decay) { return t >= 0.0 ? std::exp (-t / decay) : 0.0; };
        auto note = [] (double base, int semitones) { return base * std::pow (2.0, semitones / 12.0); };

        float peak = 0.0f;

        for (int frame = 0; frame < frames; ++frame)
        {
            const auto t = static_cast<double> (frame) / demoRate;
            const auto bar = static_cast<int> (t / secondsPerBar);
            const auto barTime = std::fmod (t, secondsPerBar);
            const auto beat = static_cast<int> (barTime / 0.5);
            const auto beatTime = std::fmod (barTime, 0.5);
            double sample = 0.0;

            switch (track)
            {
                case 0: // drums
                {
                    if (beat == 0 || beat == 2)
                        sample += std::sin (twoPi * (58.0 - 28.0 * beatTime) * beatTime)
                                * env (beatTime, 0.12) * 0.92;

                    if (beat == 1 || beat == 3)
                        sample += static_cast<double> (noise()) * env (beatTime, 0.075) * 0.48;

                    const auto hatTime = std::fmod (beatTime, 0.25);
                    sample += static_cast<double> (noise()) * env (hatTime, 0.018) * 0.20;
                    break;
                }

                case 1: // bass
                {
                    static const int notes[4] { 0, 0, 3, 5 };
                    const auto frequency = note (55.0, notes[bar % 4]);
                    sample = (std::sin (twoPi * frequency * t)
                            + 0.28 * std::sin (twoPi * frequency * 2.0 * t))
                           * env (beatTime, 0.22) * 0.42;
                    break;
                }

                case 2: // keys
                {
                    static const int chords[4][3] { { 0, 4, 7 }, { 5, 9, 12 }, { 7, 11, 14 }, { 3, 7, 10 } };
                    for (const auto semitone : chords[bar % 4])
                        sample += std::sin (twoPi * note (220.0, semitone) * t) * 0.10;

                    sample *= 0.45 + 0.55 * env (barTime, 0.75);
                    break;
                }

                case 3: // vocal guide
                {
                    static const int melody[4] { 7, 10, 12, 7 };
                    const auto frequency = note (220.0, melody[bar % 4]);
                    const auto vibrato = 1.0 + 0.006 * std::sin (twoPi * 5.2 * t);
                    sample = std::sin (twoPi * frequency * vibrato * t)
                           * (0.38 + 0.28 * std::sin (twoPi * 5.2 * t))
                           * env (barTime, 0.95) * 0.34;
                    break;
                }

                case 4: // guitar
                {
                    static const int riff[4] { 0, 3, 5, 7 };
                    const auto frequency = note (110.0, riff[beat % 4]);
                    sample = (std::sin (twoPi * frequency * t)
                            + 0.35 * std::sin (twoPi * frequency * 2.0 * t))
                           * env (beatTime, 0.13) * 0.32;
                    break;
                }

                case 5: // percussion
                {
                    const auto hatTime = std::fmod (beatTime, 0.25);
                    sample = static_cast<double> (noise()) * env (hatTime, 0.022) * 0.24;
                    break;
                }

                case 6: // plate return guide
                    sample = (std::sin (twoPi * 220.0 * t)
                            + std::sin (twoPi * 277.18 * t)
                            + std::sin (twoPi * 329.63 * t))
                           * 0.035 * (0.55 + 0.45 * std::sin (twoPi * t / 8.0));
                    break;

                default: // delay return guide
                    sample = static_cast<double> (noise()) * 0.018;
                    break;
            }

            const auto value = static_cast<float> (sample);
            data[frame] = value;
            peak = juce::jmax (peak, std::abs (value));
        }

        // A demo record needs a mix, not eight full-scale loops. The targets
        // leave headroom for the master and keep the return prints behind the
        // dry tracks.
        constexpr float trackPeaks[demoTrackCount] { 0.50f, 0.35f, 0.25f, 0.22f,
                                                 0.24f, 0.18f, 0.12f, 0.08f,
                                                 0.20f, 0.18f, 0.22f, 0.10f,
                                                 0.10f, 0.15f, 0.08f, 0.08f };

        if (peak > 0.0f)
            for (int frame = 0; frame < frames; ++frame)
                data[frame] = data[frame] / peak * trackPeaks[track];

        demoAssets[static_cast<size_t> (track)] = addAsset (std::move (asset));
    }
}

int AudioEngine::setTimelineClips (std::vector<TimelineClip> newClips)
{
    Command clear;
    clear.type = CommandType::clear;

    if (! pushCommand (clear))
        return static_cast<int> (newClips.size());

    const auto cap = engine::truncateClipCount (static_cast<int> (newClips.size()), maxClips);
    int accepted = 0;
    double endBar = 1.0;

    for (const auto& clip : newClips)
    {
        if (accepted >= cap)
            break;

        if (clip.assetId < 0 || clip.assetId >= static_cast<int> (assets.size()))
            continue;

        if (! juce::isPositiveAndBelow (clip.track, trackCount))
            continue;

        Command add;
        add.type = CommandType::add;
        add.clip.asset = assets[static_cast<size_t> (clip.assetId)].get();
        add.clip.track = clip.track;
        add.clip.startBar = juce::jmax (1.0, clip.startBar);
        add.clip.lengthBars = juce::jmax (0.0, clip.lengthBars);
        add.clip.sourceOffsetSeconds = juce::jmax (0.0, clip.sourceOffsetSeconds);
        add.clip.gainDb = clip.gainDb;
        add.clip.fadeInBars = juce::jmax (0.0f, clip.fadeInBars);
        add.clip.fadeOutBars = juce::jmax (0.0f, clip.fadeOutBars);

        if (! pushCommand (add))
            break;

        endBar = juce::jmax (endBar, add.clip.startBar + add.clip.lengthBars);
        ++accepted;
    }

    contentEnd.store (endBar);

    return static_cast<int> (newClips.size()) - accepted;
}

bool AudioEngine::pushCommand (const Command& command)
{
    const auto write = commandWrite.load (std::memory_order_relaxed);
    const auto read = commandRead.load (std::memory_order_acquire);

    if (write - read >= static_cast<juce::uint32> (commandCapacity))
        return false;

    commandQueue[write % static_cast<juce::uint32> (commandCapacity)] = command;
    commandWrite.store (write + 1, std::memory_order_release);
    return true;
}

void AudioEngine::processCommands()
{
    auto read = commandRead.load (std::memory_order_relaxed);
    const auto write = commandWrite.load (std::memory_order_acquire);

    while (read != write)
    {
        const auto& command = commandQueue[read % static_cast<juce::uint32> (commandCapacity)];

        if (command.type == CommandType::clear)
        {
            playbackClipCount = 0;
        }
        else if (playbackClipCount < maxClips)
        {
            playbackClips[static_cast<size_t> (playbackClipCount)] = command.clip;
            ++playbackClipCount;
        }

        ++read;
    }

    commandRead.store (read, std::memory_order_release);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate.store (device != nullptr ? device->getCurrentSampleRate() : 48000.0);
    bufferSize.store (device != nullptr ? device->getCurrentBufferSizeSamples() : 0);
    deviceOpen.store (device != nullptr);
    deviceInfo = device != nullptr ? device->getName() : "no audio device";
    captureAvailable.store (device != nullptr
                            && device->getActiveInputChannels().countNumberOfSetBits() > 0);
    smoothingPrimed = false;
    resetRequested.store (true);

    mixBuffer.setSize (2, juce::jmax (bufferSize.load(), 4096), false, true, false);
    trackBlock.setSize (2 * trackCount, juce::jmax (bufferSize.load(), 16384), false, true, false);

    const auto maxBlock = juce::jmax (1, bufferSize.load());
    const auto active = juce::jlimit (0, trackCount, activeTrackCount.load());

    for (int i = 0; i < active; ++i)
        mixRack[static_cast<size_t> (i)].prepare (sampleRate.load(), maxBlock);

    masterRack.prepare (sampleRate.load(), maxBlock);
    preparedRackCount.store (active);
    pluginHost.prepare (sampleRate.load(), maxBlock);
    inserts.prepare (sampleRate.load(), maxBlock, active);
}

void AudioEngine::audioDeviceStopped()
{
    automationRetired.clear();
    deviceOpen.store (false);
    bufferSize.store (0);
    dspLoadValue.store (0.0f);
    masterPeakValue.store (0.0f);

    for (auto& track : tracks)
        track.peak.store (0.0f);

    for (auto& rack : mixRack)
        rack.reset();

    masterRack.reset();
    preparedRackCount.store (0);
    inserts.collectGarbage();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    // An offline mixdown owns the engine. Render silence rather than run a
    // second pass into the device, and touch no shared state while doing it.
    if (offlineRender.load (std::memory_order_acquire))
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

        return;
    }

    const auto startTicks = juce::Time::getHighResolutionTicks();

    renderBlock (inputChannelData, numInputChannels, outputChannelData,
                 numOutputChannels, numSamples);

    // Measured callback load, not a sine wave pretending to be a DSP meter.
    const auto elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const auto budget = static_cast<double> (numSamples) / juce::jmax (1.0, sampleRate.load());
    const auto ratio = budget > 0.0 ? static_cast<float> (elapsed / budget) : 0.0f;
    dspLoadValue.store (juce::jlimit (0.0f, 1.0f, dspLoadValue.load() * 0.82f + juce::jlimit (0.0f, 1.0f, ratio) * 0.18f));
}

void AudioEngine::renderBlock (const float* const* inputChannelData, int numInputChannels,
                               float* const* outputChannelData, int numOutputChannels, int numSamples)
{

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    if (numOutputChannels == 0 || numSamples <= 0)
        return;

    processCommands();

    // A device starting up asks for a transport reset. An export in progress is
    // not the place for that: it keeps the request pending for the next live block.
    if (! offlineRender.load (std::memory_order_acquire) && resetRequested.exchange (false))
    {
        clickPhase = 0.0;
        clickEnvelope = 0.0;
        clickOscillator = 0.0;
        clickBeat = 0;
        positionSamples.store (0);
    }

    if (seekRequested.exchange (false))
    {
        clickPhase = 0.0;
        clickEnvelope = 0.0;
        clickOscillator = 0.0;
        clickBeat = 0;
    }

    if (! smoothingPrimed)
    {
        for (int track = 0; track < trackCount; ++track)
        {
            smoothedGain[track] = tracks[static_cast<size_t> (track)].gain.load();
            smoothedPan[track] = tracks[static_cast<size_t> (track)].pan.load();
        }

        smoothedMaster = masterGainValue.load();
        smoothingPrimed = true;
    }

    const auto rate = sampleRate.load();
    const auto tempo = bpm.load();
    const auto isPlaying = playing.load();
    const auto clickOn = metronome.load();

    if (rate <= 0.0)
        return;

    const auto secondsPerBar = engine::secondsPerBar (tempo);
    const auto beatSamples = rate * 60.0 / juce::jmax (1.0, tempo);

    bool anySolo = false;

    for (const auto& track : tracks)
        anySolo = anySolo || track.soloed.load();

    for (int i = 0; i < playbackClipCount; ++i)
    {
        const auto& clip = playbackClips[static_cast<size_t> (i)];
        auto& render = renderScratch[static_cast<size_t> (i)];
        render.asset = nullptr;

        if (clip.asset == nullptr || assetFrameCount (*clip.asset) <= 0)
            continue;

        const auto lengthSeconds = juce::jmax (0.0, clip.lengthBars * secondsPerBar);

        if (lengthSeconds <= 0.0)
            continue;

        const auto startSeconds = engine::barToSeconds (clip.startBar, tempo);
        const auto startSample = static_cast<int64_t> (std::llround (startSeconds * rate));
        const auto endSample = static_cast<int64_t> (std::llround ((startSeconds + lengthSeconds) * rate));

        if (endSample <= startSample)
            continue;
        render.asset = clip.asset;
        render.track = clip.track;
        render.startSample = startSample;
        render.endSample = endSample;
        render.sourceOffsetSeconds = clip.sourceOffsetSeconds;
        render.lengthSeconds = lengthSeconds;
        render.fadeInSeconds = juce::jlimit (0.0, lengthSeconds, clip.fadeInBars * secondsPerBar);
        render.fadeOutSeconds = juce::jlimit (0.0, lengthSeconds, clip.fadeOutBars * secondsPerBar);
        render.gain = juce::Decibels::decibelsToGain (clip.gainDb);
    }

    const auto loopStart = static_cast<int64_t> (std::llround (
        engine::barToSeconds (loopStartBar.load(), tempo) * rate));
    const auto loopEnd = static_cast<int64_t> (std::llround (
        engine::barToSeconds (loopEndBar.load(), tempo) * rate));
    const auto loop = loopEnabled.load() && loopEnd > loopStart;

    auto position = positionSamples.load();

    if (isPlaying && loop && position >= loopEnd)
    {
        position = loopStart;
        clickPhase = 0.0;
        clickEnvelope = 0.0;
        clickOscillator = 0.0;
        clickBeat = 0;
    }

    if (recording.load (std::memory_order_acquire))
        captureInputBlock (inputChannelData, numInputChannels, numSamples, position);

    float blockPeaks[trackCount] {};
    float masterPeak = 0.0f;
    float masterPeakLeft = 0.0f;
    float masterPeakRight = 0.0f;

    // The master rack and the optional external insert both need a block
    // buffer. mixBuffer is allocated once in audioDeviceAboutToStart().
    const auto blockStartSamples = position;
    const auto bufferReady = mixBuffer.getNumChannels() >= 2
                             && mixBuffer.getNumSamples() >= numSamples;
    auto* const mixLeft = bufferReady ? mixBuffer.getWritePointer (0) : nullptr;
    auto* const mixRight = bufferReady ? mixBuffer.getWritePointer (1) : nullptr;

    if (bufferReady)
        mixBuffer.clear (0, numSamples);

    const auto trackBufferReady = trackBlock.getNumChannels() >= 2 * trackCount
                                  && trackBlock.getNumSamples() >= numSamples;

    if (trackBufferReady)
        trackBlock.clear (0, numSamples);

    // Per-sample track sums live in the callback frame, not the sample loop.
    std::array<float, trackCount> trackLeftScratch {};
    std::array<float, trackCount> trackRightScratch {};

    // Pass 1: render clips into one stereo lane per track. Inserts run after
    // the whole block is rendered, so no plugin is called once per sample.
    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (! isPlaying)
            continue;

        std::fill (trackLeftScratch.begin(), trackLeftScratch.end(), 0.0f);
        std::fill (trackRightScratch.begin(), trackRightScratch.end(), 0.0f);

        for (int clipIndex = 0; clipIndex < playbackClipCount; ++clipIndex)
        {
            const auto& clip = renderScratch[static_cast<size_t> (clipIndex)];

            if (clip.asset == nullptr
                || position < clip.startSample
                || position >= clip.endSample)
                continue;

            const auto localSeconds = static_cast<double> (position - clip.startSample) / rate;
            const auto sourceSeconds = clip.sourceOffsetSeconds + localSeconds;
            const auto sourceFrame = sourceSeconds * clip.asset->sampleRate;
            const auto sourceFrames = assetFrameCount (*clip.asset);

            if (sourceFrame < 0.0)
                continue;

            const auto frame0 = static_cast<int64_t> (std::floor (sourceFrame));

            if (frame0 < 0 || frame0 >= sourceFrames)
                continue;

            const auto frame1 = juce::jmin (frame0 + 1, sourceFrames - 1);
            const auto fraction = static_cast<float> (sourceFrame - static_cast<double> (frame0));

            const auto fade = static_cast<float> (engine::clipGainAt (localSeconds,
                                                                      clip.lengthSeconds,
                                                                      clip.fadeInSeconds,
                                                                      clip.fadeOutSeconds));
            const auto gain = clip.gain * fade;

            // RAM or streamed window: one accessor serves both, so a
            // streamed clip that is not resident yet contributes silence
            // instead of stalling the callback on the disk.
            const auto start0 = sampleAt (*clip.asset, frame0, 0);
            const auto end0 = sampleAt (*clip.asset, frame1, 0);
            const auto start1 = sampleAt (*clip.asset, frame0, 1);
            const auto end1 = sampleAt (*clip.asset, frame1, 1);

            const auto sample0 = static_cast<float> (start0 + (end0 - start0) * fraction);
            const auto sample1 = static_cast<float> (start1 + (end1 - start1) * fraction);

            const auto lane = static_cast<size_t> (juce::jlimit (0, trackCount - 1, clip.track));
            trackLeftScratch[lane] += sample0 * gain;
            trackRightScratch[lane] += sample1 * gain;
        }

        if (trackBufferReady)
        {
            for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
            {
                trackBlock.setSample (trackIndex * 2, sample,
                                      trackLeftScratch[static_cast<size_t> (trackIndex)]);
                trackBlock.setSample (trackIndex * 2 + 1, sample,
                                      trackRightScratch[static_cast<size_t> (trackIndex)]);
            }
        }

        ++position;

        if (loop && position >= loopEnd)
        {
            position = loopStart;
            clickPhase = 0.0;
            clickEnvelope = 0.0;
            clickOscillator = 0.0;
            clickBeat = 0;
        }
    }

    // Pass 2: inserts and PDC happen once per block, before the native strip.
    if (trackBufferReady)
    {
        inserts.beginBlock();
        const auto insertBlock = engine::blockContext (blockStartSamples, numSamples, tempo, rate,
                                                       isPlaying, loop, loopStart, loopEnd);

        for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        {
            juce::AudioBuffer<float> lane (trackBlock.getArrayOfWritePointers() + trackIndex * 2,
                                           2, numSamples);
            inserts.processTrack (trackIndex, lane, insertBlock);
        }

        inserts.endBlock();
    }

    // Automation is evaluated once per block, like Ardour's per-cycle faders,
    // and the existing per-sample smoothing turns each step into a ramp.
    float automatedGain[trackCount];
    float automatedPan[trackCount];
    const auto blockBeat = static_cast<double> (blockStartSamples)
                           / juce::jmax (1.0, sampleRate.load())
                           * (tempo / 60.0);

    for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
    {
        automatedGain[trackIndex] = tracks[static_cast<size_t> (trackIndex)].gain.load();
        automatedPan[trackIndex] = tracks[static_cast<size_t> (trackIndex)].pan.load();

        if (const auto* lane = volumeAutomation[static_cast<size_t> (trackIndex)].live.load (std::memory_order_acquire);
            lane != nullptr && ! lane->empty())
            automatedGain[trackIndex] = lane->valueAt (blockBeat);

        if (const auto* lane = panAutomation[static_cast<size_t> (trackIndex)].live.load (std::memory_order_acquire);
            lane != nullptr && ! lane->empty())
            automatedPan[trackIndex] = lane->valueAt (blockBeat);
    }

    // Pass 3: native strip, fader/pan, click and master gain.
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float left = 0.0f;
        float right = 0.0f;
        float click = 0.0f;

        if (isPlaying)
        {
            for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
            {
                const auto& track = tracks[static_cast<size_t> (trackIndex)];

                smoothedGain[trackIndex] += (automatedGain[trackIndex] - smoothedGain[trackIndex]) * smoothingCoefficient;
                smoothedPan[trackIndex] += (automatedPan[trackIndex] - smoothedPan[trackIndex]) * smoothingCoefficient;

                const auto audible = ! track.muted.load() && (! anySolo || track.soloed.load());
                const auto gain = audible ? smoothedGain[trackIndex] : 0.0f;
                const auto angle = (smoothedPan[trackIndex] + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                const auto panLeft = std::cos (angle);
                const auto panRight = std::sin (angle);

                const auto sourceLeft = trackBufferReady
                                            ? trackBlock.getSample (trackIndex * 2, sample) : 0.0f;
                const auto sourceRight = trackBufferReady
                                             ? trackBlock.getSample (trackIndex * 2 + 1, sample) : 0.0f;

                auto trackLeft = sourceLeft * gain * panLeft;
                auto trackRight = sourceRight * gain * panRight;

                // The channel strip is baked into every track. It runs after
                // the inserts/PDC and before the bus sum.
                mixRack[static_cast<size_t> (trackIndex)].processSample (trackLeft, trackRight);

                left += trackLeft;
                right += trackRight;

                blockPeaks[trackIndex] = juce::jmax (blockPeaks[trackIndex],
                                                     juce::jmax (std::abs (trackLeft), std::abs (trackRight)));
            }

            if (clickPhase >= beatSamples)
            {
                clickPhase -= beatSamples;
                clickEnvelope = 0.85;
                clickOscillator = 0.0;
                ++clickBeat;
            }

            clickPhase += 1.0;

            if (clickOn && clickEnvelope > 0.0001)
            {
                const auto frequency = clickBeat % 4 == 0 ? 1600.0 : 1000.0;
                clickOscillator += twoPi * frequency / rate;
                click = static_cast<float> (std::sin (clickOscillator) * clickEnvelope * 0.3);
                clickEnvelope *= 0.9994;
            }
        }

        smoothedMaster += (masterGainValue.load() - smoothedMaster) * smoothingCoefficient;
        left = (left + click) * smoothedMaster;
        right = (right + click) * smoothedMaster;

        if (mixLeft != nullptr)
        {
            mixLeft[sample] = left;
            mixRight[sample] = right;
        }
        else
        {
            // A block larger than the prepared ceiling is a device change we
            // have not seen yet; keep it bounded rather than writing garbage.
            if (outputChannelData[0] != nullptr)
                outputChannelData[0][sample] = juce::jlimit (-1.0f, 1.0f, left);

            if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
                outputChannelData[1][sample] = juce::jlimit (-1.0f, 1.0f, right);
        }
    }

    if (mixLeft != nullptr)
    {
        juce::AudioBuffer<float> mixed (mixBuffer.getArrayOfWritePointers(), 2, numSamples);

        masterRack.process (mixed, numSamples);

        pluginHost.processBlock (mixed, engine::blockContext (blockStartSamples, numSamples, tempo, rate,
                                                              isPlaying, loop, loopStart, loopEnd));

        // Monitor section: mono fold and dim sit after the insert, so they
        // change what the engineer hears without touching the saved mix.
        if (masterMono.load())
        {
            auto* left = mixed.getWritePointer (0);
            auto* right = mixed.getWritePointer (1);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto mid = 0.5f * (left[i] + right[i]);
                left[i] = mid;
                right[i] = mid;
            }
        }

        if (masterDim.load())
            mixed.applyGain (0, numSamples, juce::Decibels::decibelsToGain (-20.0f));

        masterPeakLeft = mixed.getMagnitude (0, 0, numSamples);
        masterPeakRight = mixed.getMagnitude (1, 0, numSamples);
        masterPeak = juce::jmax (masterPeakLeft, masterPeakRight);

        for (int channel = 0; channel < juce::jmin (2, numOutputChannels); ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[channel],
                                                   mixed.getReadPointer (channel), numSamples);
    }
    else
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            masterPeakLeft = juce::jmax (masterPeakLeft,
                                         std::abs (outputChannelData[0] != nullptr ? outputChannelData[0][sample] : 0.0f));
            masterPeakRight = juce::jmax (masterPeakRight,
                                          std::abs (numOutputChannels > 1 && outputChannelData[1] != nullptr
                                                        ? outputChannelData[1][sample] : 0.0f));
        }

        masterPeak = juce::jmax (masterPeakLeft, masterPeakRight);
    }

    positionSamples.store (position);

    for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        tracks[static_cast<size_t> (trackIndex)].peak.store (blockPeaks[trackIndex]);

    masterPeakValue.store (masterPeak);
    masterPeakLeftValue.store (masterPeakLeft);
    masterPeakRightValue.store (masterPeakRight);

}

juce::String AudioEngine::masterPluginIdentifier() const
{
    return pluginHost.loadedIdentifier();
}

juce::String AudioEngine::masterPluginStateBase64() const
{
    return pluginHost.stateBase64();
}

bool AudioEngine::restoreMasterPlugin (const juce::String& identifier, const juce::String& stateBase64)
{
    auto description = catalog.findDescription (identifier);

    if (! description.has_value())
    {
        juce::String resolveError;
        description = catalog.resolveDescription (identifier, resolveError);
    }

    if (! description.has_value())
        return false;

    juce::String error;

    if (! pluginHost.loadDescription (*description, juce::jmax (1.0, sampleRate.load()),
                                      juce::jmax (1, bufferSize.load()), error))
        return false;

    if (stateBase64.isNotEmpty() && ! pluginHost.applyStateBase64 (stateBase64))
        return false;

    return true;
}

void AudioEngine::setPluginStatus (juce::String message)
{
    if (onPluginStatus)
        onPluginStatus (std::move (message));
}

bool AudioEngine::loadTrackInsert (int track, int slot, const juce::PluginDescription& description,
                                   juce::String& error)
{
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load()) || ! juce::isPositiveAndBelow (slot, 4))
    {
        error = "Insert target is outside the session";
        return false;
    }

    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto block = juce::jmax (1, bufferSize.load());
    auto alive = engineAlive;

    catalog.createAsync (description, rate, block,
                         [this, alive, track, slot, description, rate, block]
                         (std::unique_ptr<juce::AudioPluginInstance> instance, juce::String creationError)
                         {
                             if (! alive->load (std::memory_order_acquire))
                                 return;

                             if (instance == nullptr)
                             {
                                 setPluginStatus ("INSERT FAILED / " + creationError);
                                 return;
                             }

                             juce::String error;
                             auto hosted = HostedInsert::create (std::move (instance), rate, block, error);

                             if (hosted == nullptr)
                             {
                                 setPluginStatus ("INSERT FAILED / " + error);
                                 return;
                             }

                             nc::engine::PluginSlotState state;
                             state.identifier = description.createIdentifierString().toStdString();
                             state.format = description.pluginFormatName.toStdString();

                             if (const auto xml = description.createXml())
                                 state.descriptionXml = xml->toString().toStdString();

                             if (! inserts.install (track, slot, std::move (hosted), state, error))
                             {
                                 setPluginStatus ("INSERT FAILED / " + error);
                                 return;
                             }

                             setPluginStatus ("INSERT / " + description.name + " / TRACK "
                                               + juce::String (track + 1) + " / INS " + juce::String (slot + 1));
                         });

    return true;
}

bool AudioEngine::loadTrackInsertFromId (int track, int slot, const juce::String& catalogId,
                                        juce::String& error)
{
    const auto description = catalog.resolveDescription (catalogId, error);

    if (! description.has_value())
    {
        if (error.isEmpty())
            error = "That plugin is not in the scanned VST3 catalog";

        return false;
    }

    return loadTrackInsert (track, slot, *description, error);
}

bool AudioEngine::restoreTrackInsert (int track, int slot, const nc::engine::PluginSlotState& state,
                                      juce::String& error)
{
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load()) || ! juce::isPositiveAndBelow (slot, 4))
    {
        error = "Insert target is outside the session";
        return false;
    }

    if (state.isEmpty())
        return false;

    std::optional<juce::PluginDescription> description;

    if (auto found = catalog.findDescription (juce::String (state.identifier)))
        description = *found;

    if (! description.has_value() && ! state.descriptionXml.empty())
    {
        if (const auto xml = juce::XmlDocument::parse (juce::String (state.descriptionXml)))
        {
            juce::PluginDescription parsed;

            if (parsed.loadFromXml (*xml))
                description = parsed;
        }
    }

    const auto displayName = description.has_value() ? description->name
                                                       : juce::String (state.identifier);

    if (! description.has_value())
    {
        inserts.markMissing (track, slot, state, displayName);
        error = "Plugin not found: " + displayName;
        return false;
    }

    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto block = juce::jmax (1, bufferSize.load());
    auto instance = catalog.createSync (*description, rate, block, error);

    if (instance == nullptr)
    {
        inserts.markMissing (track, slot, state, displayName);
        return false;
    }

    auto hosted = HostedInsert::create (std::move (instance), rate, block, error);

    if (hosted == nullptr)
    {
        inserts.markMissing (track, slot, state, displayName);
        return false;
    }

    if (! hosted->setState (state))
    {
        error = "The saved plugin state could not be restored";
        inserts.markMissing (track, slot, state, displayName);
        return false;
    }

    return inserts.install (track, slot, std::move (hosted), state, error);
}

void AudioEngine::unloadTrackInsert (int track, int slot)
{
    inserts.remove (track, slot);
}

bool AudioEngine::moveTrackInsert (int track, int from, int to)
{
    return inserts.moveInsert (track, from, to);
}

void AudioEngine::setTrackInsertBypassed (int track, int slot, bool bypassed)
{
    inserts.setBypassed (track, slot, bypassed);
}

juce::String AudioEngine::trackInsertName (int track, int slot) const
{
    return inserts.slotName (track, slot);
}

bool AudioEngine::trackInsertIsMissing (int track, int slot) const
{
    return inserts.isMissing (track, slot);
}

nc::engine::PluginSlotState AudioEngine::trackInsertState (int track, int slot) const
{
    return inserts.slotState (track, slot);
}

int AudioEngine::trackInsertLatency (int track, int slot) const
{
    if (! juce::isPositiveAndBelow (track, trackCount) || ! juce::isPositiveAndBelow (slot, 4))
        return 0;

    const auto& insert = inserts.slot (track, slot);
    auto* processor = insert.active.load (std::memory_order_acquire);
    return processor != nullptr ? processor->latencySamples() : 0;
}

MixRackChannel& AudioEngine::mixChannel (int track)
{
    return mixRack[static_cast<size_t> (juce::jlimit (0, trackCount - 1, track))];
}

const MixRackChannel& AudioEngine::mixChannel (int track) const
{
    return mixRack[static_cast<size_t> (juce::jlimit (0, trackCount - 1, track))];
}

juce::String AudioEngine::mixRackStateJson() const
{
    auto* root = new juce::DynamicObject();
    juce::Array<juce::var> trackArray;
    const auto active = juce::jlimit (0, trackCount, activeTrackCount.load());

    for (int i = 0; i < active; ++i)
        trackArray.add (mixRack[static_cast<size_t> (i)].toVar());

    root->setProperty ("tracks", trackArray);
    root->setProperty ("master", masterRack.toVar());
    return juce::JSON::toString (juce::var (root));
}

bool AudioEngine::restoreMixRackStateJson (const juce::String& state)
{
    const auto parsed = juce::JSON::parse (state);

    if (! parsed.isObject())
        return false;

    const auto tracksVar = parsed.getProperty ("tracks", {});
    const auto active = juce::jlimit (0, trackCount, activeTrackCount.load());

    if (tracksVar.isArray())
    {
        const auto count = juce::jmin (active, tracksVar.size());

        for (int i = 0; i < count; ++i)
            mixRack[static_cast<size_t> (i)].fromVar (tracksVar[static_cast<int> (i)]);
    }

    const auto masterVar = parsed.getProperty ("master", {});

    if (masterVar.isObject())
        masterRack.fromVar (masterVar);

    // A restored project owns its mix, but the vocal role stays a property of
    // the track itself: vocal tracks keep the Open Mic stage unless the saved
    // session recorded an explicit override.
    applyVocalRoles();

    return true;
}

void AudioEngine::applyVocalRoles()
{
    const auto& tracks = sessionTracks();

    for (int track = 0; track < trackCount; ++track)
        mixRack[static_cast<size_t> (track)]
            .setVocal (isVocalTrackName (tracks[static_cast<size_t> (track)].name));
}

//==============================================================================
void AudioEngine::prepareForRender (double rate, int maxBlock)
{
    const auto block = juce::jmax (1, maxBlock);

    sampleRate.store (rate);
    smoothingPrimed = false;
    resetRequested.store (false);
    seekRequested.store (false);

    // Sized exactly as audioDeviceAboutToStart() does, so renderBlock() never
    // sees a block larger than the scratch it was promised.
    mixBuffer.setSize (2, juce::jmax (block, 4096), false, true, false);
    trackBlock.setSize (2 * trackCount, juce::jmax (block, 16384), false, true, false);

    const auto active = juce::jlimit (0, trackCount, activeTrackCount.load());

    for (int i = 0; i < active; ++i)
        mixRack[static_cast<size_t> (i)].prepare (rate, block);

    masterRack.prepare (rate, block);
    preparedRackCount.store (active);
    pluginHost.prepare (rate, block);
    inserts.prepare (rate, block, active);
}

bool AudioEngine::renderToWav (const juce::File& destination,
                               double startBar,
                               double endBar,
                               int bitDepth,
                               juce::String& error)
{
    error.clear();

    if (destination == juce::File())
    {
        error = "No destination file was chosen.";
        return false;
    }

    if (! (endBar > startBar))
    {
        error = "The export range is empty.";
        return false;
    }

    const auto rate = sampleRate.load() > 0.0 ? sampleRate.load() : 48000.0;
    constexpr int blockSize = 512;

    // The device callback owns mixBuffer/trackBlock and the render scratch, so
    // it is muted for the offline pass. Detaching it instead would restart the
    // device, and audioDeviceAboutToStart() resets the transport on the way in.
    offlineRender.store (true, std::memory_order_release);

    // Let a block that is already in flight finish before resizing the scratch.
    const auto settleMs = static_cast<int> (2000.0 * juce::jmax (1, bufferSize.load()) / rate) + 20;
    juce::Thread::sleep (settleMs);

    const auto previousPosition = positionSamples.load();
    const auto previousPlaying = playing.load();
    const auto previousLoop = loopEnabled.load();
    const auto previousMetronome = metronome.load();

    prepareForRender (rate, blockSize);

    const auto restore = [this, previousPosition, previousPlaying, previousLoop, previousMetronome]
    {
        playing.store (previousPlaying);
        loopEnabled.store (previousLoop);
        metronome.store (previousMetronome);
        offlineRender.store (false, std::memory_order_release);

        // Last writes win. A device-open reset raised while the engine was muted
        // (audioDeviceAboutToStart() raises one) is consumed by the callback and
        // zeroes the playhead, so clear it and restore the position afterwards.
        resetRequested.store (false);
        seekRequested.store (false);
        positionSamples.store (previousPosition);
    };

    const auto tempo = bpm.load();
    const auto startSeconds = engine::barToSeconds (startBar, tempo);
    const auto endSeconds = engine::barToSeconds (endBar, tempo);
    const auto lengthFrames = static_cast<int64_t> (
        std::llround ((endSeconds - startSeconds) * rate));

    if (lengthFrames <= 0)
    {
        restore();
        error = "The export range is shorter than one sample.";
        return false;
    }

    // Every track is delayed to the worst chain, then the native master rack and
    // the master insert add their own. planRender() is the rule the latency
    // tests already cover: pre-roll that many frames, drop exactly as many.
    const auto chainLatency = inserts.worstTrackLatency()
                              + juce::jmax (0, masterRack.latencySamples())
                              + inserts.masterLatency();
    const auto plan = engine::planRender (chainLatency, 0);

    const auto startFrame = static_cast<int64_t> (std::llround (startSeconds * rate));

    positionSamples.store (startFrame - plan.prerollFrames);
    playing.store (true);
    loopEnabled.store (false);   // an export is the range, not the loop
    metronome.store (false);     // the click never prints

    std::unique_ptr<juce::FileOutputStream> stream (destination.createOutputStream());

    if (stream == nullptr)
    {
        restore();
        error = "The destination could not be opened for writing.";
        return false;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), rate, 2, bitDepth, {}, 0));

    if (writer == nullptr)
    {
        restore();
        error = "The WAV writer could not be created at that sample rate and depth.";
        return false;
    }

    stream.release();   // the writer owns the stream from here

    juce::AudioBuffer<float> block (2, blockSize);
    float* channels[2] { block.getWritePointer (0), block.getWritePointer (1) };

    auto toDrop = plan.dropFrames;
    auto remaining = plan.prerollFrames + lengthFrames;
    auto framesWritten = static_cast<int64_t> (0);

    while (remaining > 0)
    {
        const auto frames = static_cast<int> (juce::jmin<int64_t> (blockSize, remaining));

        renderBlock (nullptr, 0, channels, 2, frames);

        auto* left = block.getReadPointer (0);
        auto* right = block.getReadPointer (1);
        auto usable = frames;

        if (toDrop > 0)
        {
            const auto skip = static_cast<int> (juce::jmin<int64_t> (toDrop, usable));
            left += skip;
            right += skip;
            usable -= skip;
            toDrop -= skip;
        }

        if (usable > 0)
        {
            const float* from[2] { left, right };
            writer->writeFromFloatArrays (from, 2, usable);
            framesWritten += usable;
        }

        remaining -= frames;
    }

    writer->flush();
    writer.reset();

    restore();

    if (framesWritten < lengthFrames)
    {
        error = "The mixdown ended early; the file may be incomplete.";
        return false;
    }

    return true;
}

//==============================================================================
void AudioEngine::RecordBuffer::prepare (int64_t capacityFrames)
{
    storage.setSize (2, static_cast<int> (juce::jmax<int64_t> (1, capacityFrames)), false, true, false);
    storage.clear();
    written = 0;
    overflowed = false;
}

void AudioEngine::RecordBuffer::reset() noexcept
{
    written = 0;
    overflowed = false;
}

void AudioEngine::RecordBuffer::push (const float* left, const float* right, int numSamples) noexcept
{
    const auto capacity = static_cast<int64_t> (storage.getNumSamples());

    if (capacity <= 0 || numSamples <= 0)
        return;

    const auto room = capacity - written;

    if (room <= 0)
    {
        overflowed = true;
        return;
    }

    const auto count = static_cast<int> (juce::jmin<int64_t> (room, numSamples));
    auto* outLeft = storage.getWritePointer (0) + written;
    auto* outRight = storage.getWritePointer (1) + written;

    if (left != nullptr)
        juce::FloatVectorOperations::copy (outLeft, left, count);
    else
        juce::FloatVectorOperations::clear (outLeft, count);

    if (right != nullptr)
        juce::FloatVectorOperations::copy (outRight, right, count);
    else
        juce::FloatVectorOperations::clear (outRight, count);

    written += count;

    if (count < numSamples)
        overflowed = true;
}

void AudioEngine::captureInputBlock (const float* const* inputChannelData,
                                     int numInputChannels,
                                     int numSamples,
                                     int64_t position)
{
    // Audio thread: no allocation, no locks, no disk. Armed tracks only, in
    // track order, so each armed track takes the next input channel (cycled when
    // there are fewer inputs than armed tracks).
    if (numInputChannels <= 0 || inputChannelData == nullptr || inputChannelData[0] == nullptr)
        numInputChannels = 0;

    if (recordFramesWritten == 0)
        recordStartFrame.store (position, std::memory_order_release);

    int inputIndex = 0;

    for (int track = 0; track < trackCount; ++track)
    {
        if (! recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            continue;

        const auto* left = numInputChannels > 0
                               ? inputChannelData[inputIndex % numInputChannels] : nullptr;
        const auto* right = numInputChannels > 1
                                ? inputChannelData[(inputIndex + 1) % numInputChannels] : left;

        recordBuffers[static_cast<size_t> (track)].push (left, right, numSamples);
        ++inputIndex;
    }

    recordFramesWritten += numSamples;
}

void AudioEngine::setRecordArmed (int track, bool armed)
{
    if (track < 0 || track >= trackCount)
        return;

    recordArmed[static_cast<size_t> (track)].store (armed, std::memory_order_release);
}

bool AudioEngine::isRecordArmed (int track) const
{
    return track >= 0 && track < trackCount
           && recordArmed[static_cast<size_t> (track)].load (std::memory_order_acquire);
}

int64_t AudioEngine::recordedFrames() const
{
    int64_t most = 0;

    for (int track = 0; track < trackCount; ++track)
        if (recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            most = juce::jmax (most, recordBuffers[static_cast<size_t> (track)].written);

    return most;
}

int AudioEngine::recordOverflows() const
{
    int count = 0;

    for (int track = 0; track < trackCount; ++track)
        if (recordBuffers[static_cast<size_t> (track)].overflowed)
            ++count;

    return count;
}

bool AudioEngine::startRecording (juce::String& error)
{
    if (recording.load (std::memory_order_acquire))
    {
        error = "Already recording";
        return false;
    }

    const auto rate = sampleRate.load();

    if (rate <= 0.0)
    {
        error = "No audio device is open";
        return false;
    }

    int armed = 0;

    for (int track = 0; track < trackCount; ++track)
        if (recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            ++armed;

    if (armed == 0)
    {
        error = "Arm at least one track first";
        return false;
    }

    // Three minutes per armed track, allocated up front so the callback never
    // allocates. A take that runs past the buffer reports an overflow instead of
    // being silently truncated; streaming capture to disk is the next step.
    const auto capacity = static_cast<int64_t> (rate * 60.0 * 3.0);

    for (int track = 0; track < trackCount; ++track)
    {
        auto& buffer = recordBuffers[static_cast<size_t> (track)];
        buffer.reset();

        if (recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            buffer.prepare (capacity);
        else if (buffer.storage.getNumSamples() > 0)
            buffer.storage.setSize (0, 0);
    }

    recordFramesWritten = 0;
    recordStartFrame.store (positionSamples.load(), std::memory_order_release);
    recording.store (true, std::memory_order_release);
    error.clear();
    return true;
}

std::vector<AudioEngine::RecordedTake> AudioEngine::stopRecording (const juce::File& folder,
                                                                  int bitDepth,
                                                                  juce::String& error)
{
    std::vector<RecordedTake> takes;

    if (! recording.exchange (false, std::memory_order_acq_rel))
    {
        error = "Not recording";
        return takes;
    }

    // Capture is closed; the buffers are no longer written from this point.
    const auto latency = static_cast<int64_t> (juce::jmax (0, recordLatency.load()));
    const auto startFrame = recordStartFrame.load (std::memory_order_acquire);
    const auto rate = sampleRate.load();

    if (! folder.isDirectory() && ! folder.createDirectory())
    {
        error = "Cannot create the recording folder";
        return takes;
    }

    juce::WavAudioFormat wav;
    int written = 0;

    for (int track = 0; track < trackCount; ++track)
    {
        if (! recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            continue;

        auto& buffer = recordBuffers[static_cast<size_t> (track)];
        const auto captured = buffer.written;
        const auto usable = static_cast<int64_t> (juce::jmax<int64_t> (0, captured - latency));

        if (usable <= 0)
            continue;

        const auto file = folder.getChildFile ("Track " + juce::String (track + 1)
                                               + " take " + juce::String (++written) + ".wav");
        file.deleteFile();

        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
        {
            error = "Cannot write " + file.getFullPathName();
            continue;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), rate, 2, bitDepth, {}, 0));

        if (writer == nullptr)
        {
            error = "Unsupported recording format";
            continue;
        }

        stream.release(); // the writer owns the stream from here

        // The head of the capture is the round-trip latency: dropping it aligns
        // the take with material that was already on the timeline.
        writer->writeFromAudioSampleBuffer (buffer.storage,
                                            static_cast<int> (latency),
                                            static_cast<int> (usable));
        writer.reset();

        RecordedTake take;
        take.track = track;
        take.file = file;
        take.startFrame = startFrame;
        take.frames = usable;
        takes.push_back (take);
    }

    for (auto& buffer : recordBuffers)
        if (buffer.storage.getNumSamples() > 0)
            buffer.storage.setSize (0, 0);

    recordFramesWritten = 0;
    return takes;
}

void AudioEngine::setTrackAutomation (int track, AutomationParam param, const AutomationLane& lane)
{
    if (track < 0 || track >= trackCount)
        return;

    auto& slot = (param == AutomationParam::Pan ? panAutomation : volumeAutomation)
                     [static_cast<size_t> (track)];

    auto next = std::make_unique<AutomationLane> (lane);

    // Retired lanes are never freed while a block may still hold the pointer;
    // audioDeviceStopped() clears them once the callback is gone.
    if (slot.owned != nullptr)
        automationRetired.push_back (std::move (slot.owned));

    slot.owned = std::move (next);
    slot.live.store (slot.owned.get(), std::memory_order_release);
}

void AudioEngine::clearTrackAutomation (int track, AutomationParam param)
{
    setTrackAutomation (track, param, {});
}

float AudioEngine::automationValue (int track, AutomationParam param, double beats) const
{
    if (track < 0 || track >= trackCount)
        return 0.0f;

    const auto& slot = (param == AutomationParam::Pan ? panAutomation : volumeAutomation)
                           [static_cast<size_t> (track)];

    const auto* lane = slot.live.load (std::memory_order_acquire);
    return lane != nullptr ? lane->valueAt (beats) : 0.0f;
}

void AudioEngine::suspendAudioCallbackForTesting()
{
    deviceManager.removeAudioCallback (this);
}

void AudioEngine::processBlockForTesting (const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples)
{
    renderBlock (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

} // namespace nc
