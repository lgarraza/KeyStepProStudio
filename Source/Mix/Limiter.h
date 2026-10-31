/*
    Limiter.h
    The master limiter of the Studio: a gain (the template's "+9 dB" boost of properly gain-staged tracks) followed by a look-ahead
    brick-wall limiter. Pure C++17 + std, no JUCE. Real time: everything is reserved in prepare().

    The signal is delayed by the look-ahead (latency() samples); the gain is pulled down smoothly BEFORE a peak reaches the output
    and released slowly, and a final clamp guarantees the output never goes above the ceiling.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace mix
{

class Limiter
{
public:
    static constexpr float kLookaheadMs = 3.0f;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        lookahead = std::max (1, (int) std::lround (kLookaheadMs * 0.001 * sampleRate));
        delayL.assign ((size_t) lookahead, 0.0f);
        delayR.assign ((size_t) lookahead, 0.0f);
        peaks.assign ((size_t) lookahead, 0.0f);
        pos = 0;
        gain = 1.0f;
        attackCoef = 1.0f - std::exp (-1.0f / (0.25f * (float) lookahead));   // reaches the target within the look-ahead
        updateRelease();
        reductionDb = 0.0f;
    }

    void reset() noexcept
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        std::fill (peaks.begin(), peaks.end(), 0.0f);
        gain = 1.0f;
        reductionDb = 0.0f;
    }

    /** Boost before the limiter, in dB (0 = none). */
    void setGainDb (float db) noexcept { inputGain = std::pow (10.0f, std::clamp (db, -24.0f, 24.0f) / 20.0f); }
    /** The level the output never exceeds, in dBFS (default -1). */
    void setCeilingDb (float db) noexcept { ceiling = std::pow (10.0f, std::clamp (db, -24.0f, 0.0f) / 20.0f); }
    void setReleaseMs (float ms) noexcept { releaseMs = std::clamp (ms, 10.0f, 2000.0f); updateRelease(); }

    int latency() const noexcept { return lookahead; }

    /** The most gain reduction applied since the last call, in dB (0 or negative). */
    float takeReductionDb() noexcept
    {
        const float r = reductionDb;
        reductionDb = 0.0f;
        return r;
    }

    /** In place. */
    void processStereo (float* left, float* right, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float inL = left[n] * inputGain, inR = right[n] * inputGain;

            peaks[(size_t) pos] = std::max (std::abs (inL), std::abs (inR));
            float windowPeak = 0.0f;
            for (float p : peaks)
                windowPeak = std::max (windowPeak, p);

            const float target = windowPeak > ceiling ? ceiling / windowPeak : 1.0f;
            if (target < gain)
                gain += (target - gain) * attackCoef;
            else
                gain += (target - gain) * releaseCoef;

            const float outL = delayL[(size_t) pos] * gain, outR = delayR[(size_t) pos] * gain;
            delayL[(size_t) pos] = inL;
            delayR[(size_t) pos] = inR;
            pos = (pos + 1) % lookahead;

            left[n] = std::clamp (outL, -ceiling, ceiling);
            right[n] = std::clamp (outR, -ceiling, ceiling);
            if (gain < 1.0f)
                reductionDb = std::min (reductionDb, 20.0f * std::log10 (std::max (gain, 1.0e-6f)));
        }
    }

private:
    void updateRelease() { releaseCoef = 1.0f - std::exp (-1.0f / (0.001f * releaseMs * (float) sampleRate)); }

    double sampleRate = 48000.0;
    int lookahead = 144, pos = 0;
    std::vector<float> delayL, delayR, peaks;
    float gain = 1.0f, inputGain = 1.0f, ceiling = 0.8912509f;   // -1 dBFS
    float attackCoef = 0.1f, releaseCoef = 0.001f, releaseMs = 200.0f, reductionDb = 0.0f;
};

} // namespace mix
