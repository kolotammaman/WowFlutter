#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

//==============================================================================
// Circular delay buffer with Hermite (Catmull-Rom) interpolation.
// All methods are real-time safe after prepare() is called.
//==============================================================================
class CircularDelayLine
{
public:
    void prepare (int maxSamples)
    {
        buffer.assign (static_cast<size_t> (maxSamples), 0.0f);
        bufSize   = maxSamples;
        writePos  = 0;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    // Write one sample and advance the write pointer.
    void write (float sample) noexcept
    {
        buffer[static_cast<size_t> (writePos)] = sample;
        if (++writePos >= bufSize)
            writePos = 0;
    }

    // Read with fractional delay (in samples). delaySamples must be >= 1.0
    // and < bufSize - 2.  The write pointer must have already been advanced
    // (i.e. call write() before read() each sample).
    float read (float delaySamples) const noexcept
    {
        int   d    = static_cast<int> (delaySamples);
        float frac = delaySamples - static_cast<float> (d);

        // tap(n) returns sample that is n positions behind the write pointer
        auto tap = [&] (int n) -> float
        {
            int idx = writePos - n;
            if (idx < 0) idx += bufSize;
            return buffer[static_cast<size_t> (idx)];
        };

        // Four points for Catmull-Rom cubic: x0..x3 centred on [x1, x2]
        float x0 = tap (d - 1);
        float x1 = tap (d);
        float x2 = tap (d + 1);
        float x3 = tap (d + 2);

        float c0 =  x1;
        float c1 =  0.5f * (x2 - x0);
        float c2 =  x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        float c3 = -0.5f * x0 + 1.5f * x1 - 1.5f * x2 + 0.5f * x3;

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    int size() const noexcept { return bufSize; }

private:
    std::vector<float> buffer;
    int bufSize  = 0;
    int writePos = 0;
};

//==============================================================================
// RBJ-cookbook biquad, transposed direct form II. Coefficients are computed
// in place, so updating them never allocates.
//==============================================================================
class Biquad
{
public:
    void reset() noexcept { z1 = z2 = 0.0f; }

    void setLowPass (double sr, double freq, double q) noexcept
    {
        const double w0 = juce::MathConstants<double>::twoPi * freq / sr;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        set ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5,
             1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    void setHighPass (double sr, double freq, double q) noexcept
    {
        const double w0 = juce::MathConstants<double>::twoPi * freq / sr;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        set ((1.0 + cw) * 0.5, -(1.0 + cw), (1.0 + cw) * 0.5,
             1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    void setPeak (double sr, double freq, double q, double gainDb) noexcept
    {
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * freq / sr;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        set (1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A,
             1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
    }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = (float) (nb0 / na0);  b1 = (float) (nb1 / na0);  b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0);  a2 = (float) (na2 / na0);
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

//==============================================================================
// Smoothed random in [-1, 1]: picks a new random point at `rateHz` and glides
// to it with a smoothstep, so the curve and its slope are both continuous.
// Slope continuity matters here: the slope of the delay time is the pitch.
//==============================================================================
class SmoothRandom
{
public:
    void prepare (double sampleRate, juce::int64 seed)
    {
        invSr = 1.0f / (float) sampleRate;
        rng.setSeed (seed);
        from  = 0.0f;   // start centred so playback doesn't open with a pitch jump
        to    = next();
        phase = 0.0f;
    }

    float process (float rateHz) noexcept
    {
        phase += rateHz * invSr;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            from = to;
            to   = next();
        }
        const float s = phase * phase * (3.0f - 2.0f * phase);
        return from + (to - from) * s;
    }

private:
    float next() noexcept { return rng.nextFloat() * 2.0f - 1.0f; }

    juce::Random rng;
    float invSr = 1.0f / 44100.0f;
    float from = 0.0f, to = 0.0f, phase = 0.0f;
};

//==============================================================================
class WowFlutterAudioProcessor  : public juce::AudioProcessor
{
public:
    WowFlutterAudioProcessor();
    ~WowFlutterAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool   acceptsMidi() const override { return false; }
    bool   producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    //==========================================================================
    int  getNumPrograms() override;
    int  getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    // Peak speed deviation of the last block, in percent. Read by the meter.
    std::atomic<float> speedDeviation { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateToneFilters (float warmth);

    CircularDelayLine delayL, delayR;

    // Shared phase accumulators for 3 oscillators.
    // R channel applies a stereo phase offset at read-time without diverging.
    std::array<float, 3> phases { 0.0f, 0.0f, 0.0f };

    // Irregularity sources. Index 0 = left, 1 = right.
    SmoothRandom wowRateDrift;
    std::array<SmoothRandom, 2> wowDrift, flutterSwell, flutterScrape;

    // Tape colour: subsonic cut, head bump, playback HF loss.
    std::array<Biquad, 2> dcCut, headBump, hfLoss;
    float lastWarmth = -1.0f;

    juce::SmoothedValue<float> wowDepthSm, flutDepthSm, widthSm, mixSm, warmthSm;

    // Fixed centre of the modulated read head, in samples. The dry path is
    // read at this exact delay so dry and wet stay time-aligned.
    int   baseDelay  = 0;
    float prevDelayL = 0.0f;

    double currentSampleRate = 44100.0;
    int    currentProgram    = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WowFlutterAudioProcessor)
};
