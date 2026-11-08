#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace nc::engine
{
/** The gain-stage circuits carried over from the Preamp topology models.

    Each id names a real front-end stage, and each one shapes the signal with
    its own transfer curve: the Neve BA283 is a single-ended Class A bipolar
    stage (asymmetric, second-harmonic bloom), the API 2520 a symmetric class AB
    discrete op-amp (third-harmonic punch), the Avalon M5 a high-voltage cascode
    (almost linear), the SSL 4000 E a complementary multistage array (tight
    transient knee), and the two REDD.47 variants the EF86 pentode and 6DJ8
    triode stages with their screen-grid and anode-follower curvature.
*/
enum class CharacterCircuit
{
    neveBa283 = 0,
    api2520,
    avalonM5,
    ssl4000,
    reddEf86,
    redd6dj8
};

inline constexpr int numCharacterCircuits = 6;

inline const char* characterCircuitName (CharacterCircuit circuit) noexcept
{
    switch (circuit)
    {
        case CharacterCircuit::neveBa283: return "NEVE BA283";
        case CharacterCircuit::api2520:   return "API 2520";
        case CharacterCircuit::avalonM5:  return "AVALON M5";
        case CharacterCircuit::ssl4000:   return "SSL 4000 E";
        case CharacterCircuit::reddEf86:  return "REDD.47 EF86";
        case CharacterCircuit::redd6dj8:  return "REDD.47 6DJ8";
    }

    return "NEVE BA283";
}

/** The four knobs every stock instrument carries: which circuit, how hard it
    is driven, how much output-iron is in the path, and a tilt for the top and
    bottom. That is the whole section, deliberately. */
struct CharacterSettings
{
    bool on = false;
    CharacterCircuit circuit = CharacterCircuit::neveBa283;
    float driveDb = 6.0f;   // 0..24 dB into the stage
    float iron = 0.30f;     // 0..1 output-transformer weight
    float tone = 0.0f;      // -1..1 tilt: negative darkens, positive opens up
    float outputDb = 0.0f;  // -24..+12 dB make-up
    float mix = 1.0f;       // 0..1 parallel blend
};

/** A stereo, allocation-free analog character section.

    Signal order is the hardware order: drive into the gain stage, then the
    output transformer, then the inductor-style tilt, then make-up trim. A DC
    blocker removes the operating-point offset the asymmetric stages introduce,
    so the section does not ride the mix bus upward.

    Threading: setSettings may be called from the message thread while process*
    runs on the audio thread; every field is read once per block into smoothing
    state, so no tearing is possible. */
class AnalogCharacter
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;

        // 25 Hz DC blocker, 700 Hz tilt crossover.
        dcCoefficient = 1.0f - static_cast<float> (juceTwoPi() * 25.0 / fs);
        tiltCoefficient = 1.0f - static_cast<float> (std::exp (-juceTwoPi() * 700.0 / fs));
        reset();
        setSettings (settings);
    }

    void reset() noexcept
    {
        dcStateL = dcStateR = 0.0f;
        dcPreviousL = dcPreviousR = 0.0f;
        tiltLowL = tiltLowR = 0.0f;
        smoothedDrive = driveGainFor (settings.driveDb);
        smoothedOutput = dbToGain (settings.outputDb);
        smoothedLow = smoothedHigh = 1.0f;
    }

    void setSettings (const CharacterSettings& newSettings) noexcept
    {
        settings = newSettings;
        settings.driveDb = std::clamp (settings.driveDb, 0.0f, 24.0f);
        settings.iron = std::clamp (settings.iron, 0.0f, 1.0f);
        settings.tone = std::clamp (settings.tone, -1.0f, 1.0f);
        settings.outputDb = std::clamp (settings.outputDb, -24.0f, 12.0f);
        settings.mix = std::clamp (settings.mix, 0.0f, 1.0f);

        const auto tilt = settings.tone;
        targetLow = dbToGain (-tilt * 6.0f);
        targetHigh = dbToGain (tilt * 6.0f);
    }

    const CharacterSettings& currentSettings() const noexcept { return settings; }

    /** Starts a block: latches targets so per-sample work stays branch-free. */
    void beginBlock() noexcept
    {
        targetDrive = settings.on ? driveGainFor (settings.driveDb) : 1.0f;
        targetOutput = dbToGain (settings.outputDb);
    }

    void processSample (float& left, float& right) noexcept
    {
        if (! settings.on)
        {
            const auto gain = smooth (smoothedOutput, targetOutput);
            left *= gain;
            right *= gain;
            return;
        }

        const auto drive = smooth (smoothedDrive, targetDrive);
        const auto makeUp = smooth (smoothedOutput, targetOutput);
        const auto lowGain = smooth (smoothedLow, targetLow);
        const auto highGain = smooth (smoothedHigh, targetHigh);

        const auto dryL = left;
        const auto dryR = right;

        auto wetL = shape (left * drive, settings);
        auto wetR = shape (right * drive, settings);

        // The asymmetric stages leave an operating-point offset behind.
        const auto dcL = wetL - dcPreviousL + dcCoefficient * dcStateL;
        const auto dcR = wetR - dcPreviousR + dcCoefficient * dcStateR;
        dcPreviousL = wetL;
        dcPreviousR = wetR;
        dcStateL = dcL;
        dcStateR = dcR;
        wetL = dcL;
        wetR = dcR;

        // Inductor-style tilt: one pole splits the band into low and high, each
        // side trimmed by the tone knob.
        tiltLowL += tiltCoefficient * (wetL - tiltLowL);
        tiltLowR += tiltCoefficient * (wetR - tiltLowR);
        wetL = tiltLowL * lowGain + (wetL - tiltLowL) * highGain;
        wetR = tiltLowR * lowGain + (wetR - tiltLowR) * highGain;

        wetL *= makeUp;
        wetR *= makeUp;

        const auto mix = settings.mix;
        left = dryL + (wetL - dryL) * mix;
        right = dryR + (wetR - dryR) * mix;

        if (! std::isfinite (left)) left = 0.0f;
        if (! std::isfinite (right)) right = 0.0f;
    }

    void processBlock (float* left, float* right, int numSamples) noexcept
    {
        beginBlock();

        for (int i = 0; i < numSamples; ++i)
            processSample (left[i], right[i]);
    }

    /** One mono sample, used by tests and by the mono voice path. */
    float processMono (float input) noexcept
    {
        float l = input, r = input;
        processSample (l, r);
        return 0.5f * (l + r);
    }

private:
    static constexpr double juceTwoPi() noexcept { return 6.283185307179586; }

    static float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

    static float driveGainFor (float driveDb) noexcept
    {
        // Half the raw gain is spent on the stage, the rest is the user's
        // loudness; this keeps 12 dB of drive from being 12 dB of level.
        return std::pow (10.0f, driveDb / 40.0f);
    }

    static float smooth (float& state, float target) noexcept
    {
        state += (target - state) * 0.0025f;
        return state;
    }

    /** Gain-stage transfer followed by the output transformer. */
    static float shape (float x, const CharacterSettings& s) noexcept
    {
        const auto drive = 1.0f + (s.driveDb / 24.0f) * 2.4f;

        // The operating-point bias is what makes each circuit asymmetric; the
        // resistor values are the ones in the Preamp topology models.
        const float naturalBias =
            s.circuit == CharacterCircuit::neveBa283 ? 0.18f
            : s.circuit == CharacterCircuit::reddEf86 ? 0.22f
            : s.circuit == CharacterCircuit::redd6dj8 ? 0.20f
            : s.circuit == CharacterCircuit::avalonM5 ? 0.02f
                                                      : 0.05f;
        const auto bias = naturalBias * 0.85f;

        float y = 0.0f;

        switch (s.circuit)
        {
            case CharacterCircuit::neveBa283:
            {
                // Ebers-Moll bipolar junction with thermal voltage and Early
                // effect flattening of the negative swing.
                const auto vt = 0.026f * (1.0f + drive * 0.08f);
                const auto biased = x * drive + bias;

                if (biased >= 0.0f)
                    y = std::tanh (biased * 1.3f / vt) * 0.78f
                      + (1.0f - std::exp (-std::min (20.0f, biased * 0.9f))) * 0.22f;
                else
                {
                    const auto z = std::max (-50.0f, biased / vt);
                    y = std::expm1 (z) * 0.85f + biased * 0.15f;
                }

                const auto z0 = std::max (-50.0f, bias / vt);
                const auto dcOffset = (std::expm1 (z0) * 0.85f + bias * 0.15f) * 0.5f;
                y -= dcOffset;
                break;
            }

            case CharacterCircuit::api2520:
            {
                // Push-pull class AB: cubic soft knee, then a symmetric rail.
                const auto biased = x * drive + bias * 0.15f;
                const auto driven = biased * 1.6f;

                y = std::abs (driven) < 1.0f
                        ? driven - (driven * driven * driven) / 3.6f
                        : std::copysign (2.6f / 3.6f + std::tanh (std::abs (driven) - 1.0f) * 0.28f, driven);
                break;
            }

            case CharacterCircuit::avalonM5:
                y = std::tanh (x * drive * 0.78f * 1.04f);
                break;

            case CharacterCircuit::ssl4000:
                y = std::tanh ((x * drive * 1.15f + bias * 0.22f) * 1.45f) / 1.18f;
                break;

            case CharacterCircuit::reddEf86:
            {
                // Pentode with screen-grid compression and an arctan knee.
                const auto biased = x * drive * 1.25f + bias;
                const auto squeeze = biased > 0.35f
                                         ? std::pow (biased - 0.35f, 2.2f) * 0.45f
                                         : 0.0f;
                y = std::atan ((biased - squeeze) * 2.4f);
                break;
            }

            case CharacterCircuit::redd6dj8:
            {
                // Triode anode follower: numerically stable softplus.
                const auto biased = x * drive * 1.18f + bias;
                const auto effective = biased + 1.0f / 33.0f;
                const auto forced = 4.2f * effective;
                const auto positive = forced > 20.0f ? forced : std::log1p (std::exp (forced));
                const auto negative = (-forced * 1.2f) > 20.0f
                                          ? (-forced * 1.2f)
                                          : std::log1p (std::exp (-forced * 1.2f));

                y = effective >= 0.0f ? std::pow (positive, 1.4f) * 0.65f
                                      : -std::pow (negative, 1.4f) * 0.62f;
                break;
            }
        }

        if (! std::isfinite (y))
            y = 0.0f;

        y = std::clamp (y, -1.0f, 1.0f);

        if (s.iron > 0.0f)
            y = transformer (y, s);

        return y;
    }

    /** Frohlich core saturation with a Jiles-Atherton style asymmetry term.
        Silicon steel (Neve/SSL) is heavier than nickel (Avalon) by design. */
    static float transformer (float x, const CharacterSettings& s) noexcept
    {
        const auto steel = s.circuit == CharacterCircuit::neveBa283
                        || s.circuit == CharacterCircuit::ssl4000;

        const auto alpha = (steel ? 2.2f / 1.65f : 2.2f / 0.95f) * (1.0f + s.iron * 1.5f)
                         * (steel ? 1.4f : 0.95f);
        const auto p = steel ? 2.2f : 3.0f;
        const auto h = x * alpha;
        const auto magnitude = std::pow (1.0f + std::pow (std::abs (h), p), 1.0f / p);
        const auto magnetisation = h / magnitude;
        const auto asymmetry = (steel ? 0.42f : 0.075f) * x * x * (x < 0.0f ? -1.0f : 1.0f);
        const auto weighted = magnetisation + asymmetry * s.iron;

        return std::tanh (weighted * 1.05f);
    }

    double fs = 48000.0;
    CharacterSettings settings {};

    float dcCoefficient = 0.996f;
    float tiltCoefficient = 0.09f;

    float dcStateL = 0.0f, dcStateR = 0.0f;
    float dcPreviousL = 0.0f, dcPreviousR = 0.0f;
    float tiltLowL = 0.0f, tiltLowR = 0.0f;

    float smoothedDrive = 1.0f, smoothedOutput = 1.0f;
    float smoothedLow = 1.0f, smoothedHigh = 1.0f;
    float targetDrive = 1.0f, targetOutput = 1.0f;
    float targetLow = 1.0f, targetHigh = 1.0f;
};
} // namespace nc::engine
