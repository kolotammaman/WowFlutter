#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Factory presets
//==============================================================================
struct WFPreset
{
    const char* name;
    float wowRate, flutterRate, wowDepth, flutterDepth, stereoWidth, dryWet, warmth, drift;
    bool  syncEnabled;
    int   wowDivision;  // index into { 4 bars, 2 bars, 1 bar, 1/2, 1/4, 1/8, 1/16 }
};

static const WFPreset kPresets[] =
{
//  name                wowRate flutRate wowDpth flutDpth sWid  mix   warm  drift sync div
    { "Fresh Cassette",   0.40f,  6.5f,   0.35f,  0.06f,  0.3f, 1.0f, 0.45f, 0.40f, false, 2 },
    { "Worn Cassette",    0.55f,  7.5f,   1.10f,  0.15f,  0.4f, 1.0f, 0.60f, 0.65f, false, 2 },
    { "Walkman",          0.90f,  9.0f,   1.60f,  0.30f,  0.3f, 1.0f, 0.70f, 0.80f, false, 2 },
    { "Reel to Reel",     0.25f,  5.0f,   0.20f,  0.03f,  0.2f, 1.0f, 0.35f, 0.25f, false, 2 },
    { "VHS Drift",        0.20f,  4.0f,   2.50f,  0.12f,  0.6f, 1.0f, 0.65f, 0.70f, false, 2 },
    { "Warped Record",    0.555f, 8.0f,   2.80f,  0.05f,  0.2f, 1.0f, 0.50f, 0.15f, false, 2 },
    { "Dictaphone",       1.40f, 11.0f,   1.40f,  0.70f,  0.2f, 1.0f, 0.90f, 0.90f, false, 2 },
    { "Seasick",          0.12f,  6.0f,   5.00f,  0.20f,  0.7f, 1.0f, 0.55f, 0.50f, false, 2 },
    { "Tape Chorus",      0.80f,  6.0f,   1.00f,  0.10f,  0.9f, 0.5f, 0.40f, 0.30f, false, 2 },
    { "Colour Only",      0.50f,  6.0f,   0.00f,  0.00f,  0.5f, 1.0f, 0.60f, 0.00f, false, 2 },
};

static constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));

// Fixed shimmer rate — not exposed as a parameter
static constexpr float kShimmerRateHz = 12.0f;

// Shimmer is always a fraction of flutter depth
static constexpr float kShimmerFrac = 0.33f;

// Note division table: value in beats (quarter notes)
static constexpr double kDivisionBeats[] = { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25 };
//                                          4 bar 2bar 1bar  1/2  1/4  1/8  1/16

// Read-head centre. Worst-case excursion is wow (1x) + flutter (~2x incl.
// swell, scrape and shimmer) at 5 ms each, so 16 ms leaves headroom.
static constexpr double kBaseDelayMs = 16.0;

//==============================================================================
WowFlutterAudioProcessor::WowFlutterAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
WowFlutterAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto withUnit = [] (const char* unit, int decimals)
    {
        return juce::AudioParameterFloatAttributes()
                   .withLabel (unit)
                   .withStringFromValueFunction ([unit, decimals] (float v, int)
                        { return juce::String (v, decimals) + " " + unit; });
    };

    auto percent = []
    {
        return juce::AudioParameterFloatAttributes()
                   .withStringFromValueFunction ([] (float v, int)
                        { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; });
    };

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "wowRate", 1 }, "Wow Rate",
        juce::NormalisableRange<float> (0.1f, 2.0f, 0.001f, 0.6f), 0.5f,
        withUnit ("Hz", 2)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "flutterRate", 1 }, "Flutter Rate",
        juce::NormalisableRange<float> (2.0f, 14.0f, 0.01f), 6.0f,
        withUnit ("Hz", 1)));

    // Depth ranges are skewed so the musical region (well under 1 ms) gets
    // most of the knob travel.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "wowDepth", 1 }, "Wow Depth",
        juce::NormalisableRange<float> (0.0f, 5.0f, 0.001f, 0.45f), 0.8f,
        withUnit ("ms", 2)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "flutterDepth", 1 }, "Flutter Depth",
        juce::NormalisableRange<float> (0.0f, 5.0f, 0.001f, 0.3f), 0.1f,
        withUnit ("ms", 2)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "stereoWidth", 1 }, "Stereo Width",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.4f, percent()));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dryWet", 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f, percent()));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "warmth", 1 }, "Warmth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f, percent()));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "drift", 1 }, "Drift",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f, percent()));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "syncEnabled", 1 }, "Sync to BPM", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "wowDivision", 1 }, "Wow Division",
        juce::StringArray { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16" },
        2));   // default: 1 bar

    return { params.begin(), params.end() };
}

//==============================================================================
bool WowFlutterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet()  != juce::AudioChannelSet::stereo()) return false;
    return true;
}

//==============================================================================
void WowFlutterAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    baseDelay = (int) std::ceil (kBaseDelayMs * 0.001 * sampleRate);
    setLatencySamples (baseDelay);

    const int maxSamples = baseDelay * 2 + 16;
    delayL.prepare (maxSamples);
    delayR.prepare (maxSamples);

    wowRateDrift.prepare (sampleRate, 0x5eed01);
    for (int ch = 0; ch < 2; ++ch)
    {
        wowDrift[(size_t) ch]     .prepare (sampleRate, 0x5eed10 + ch);
        flutterSwell[(size_t) ch] .prepare (sampleRate, 0x5eed20 + ch);
        flutterScrape[(size_t) ch].prepare (sampleRate, 0x5eed30 + ch);
    }

    for (auto* sm : { &wowDepthSm, &flutDepthSm, &widthSm, &mixSm, &warmthSm })
        sm->reset (sampleRate, 0.05);

    wowDepthSm .setCurrentAndTargetValue (apvts.getRawParameterValue ("wowDepth")    ->load());
    flutDepthSm.setCurrentAndTargetValue (apvts.getRawParameterValue ("flutterDepth")->load());
    widthSm    .setCurrentAndTargetValue (apvts.getRawParameterValue ("stereoWidth") ->load());
    mixSm      .setCurrentAndTargetValue (apvts.getRawParameterValue ("dryWet")      ->load());
    warmthSm   .setCurrentAndTargetValue (apvts.getRawParameterValue ("warmth")      ->load());

    for (auto& f : dcCut)
        f.setHighPass (sampleRate, 18.0, 0.7071);

    lastWarmth = -1.0f;
    updateToneFilters (warmthSm.getCurrentValue());

    reset();
}

void WowFlutterAudioProcessor::reset()
{
    delayL.reset();
    delayR.reset();
    phases.fill (0.0f);
    prevDelayL = (float) baseDelay;

    for (int ch = 0; ch < 2; ++ch)
    {
        dcCut[(size_t) ch].reset();
        headBump[(size_t) ch].reset();
        hfLoss[(size_t) ch].reset();
    }
}

//==============================================================================
// Warmth moves three things together, the way a hotter, older machine would:
// a bigger low-end head bump, more high-frequency loss on playback, and more
// saturation (handled per sample in processBlock).
//==============================================================================
void WowFlutterAudioProcessor::updateToneFilters (float warmth)
{
    if (std::abs (warmth - lastWarmth) < 0.002f)
        return;

    lastWarmth = warmth;
    const double sr     = currentSampleRate;
    const double bumpDb = 4.5 * warmth;
    const double cutoff = juce::jmin (20000.0 * std::pow (0.4, (double) warmth), sr * 0.45);

    for (int ch = 0; ch < 2; ++ch)
    {
        headBump[(size_t) ch].setPeak (sr, 90.0, 0.7, bumpDb);
        hfLoss[(size_t) ch]  .setLowPass (sr, cutoff, 0.62);
    }
}

//==============================================================================
void WowFlutterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const float sr = static_cast<float> (currentSampleRate);

    // ------------------------------------------------------------------
    // Read parameters
    // ------------------------------------------------------------------
    float wowRate             = apvts.getRawParameterValue ("wowRate")     ->load();
    const float flutRate      = apvts.getRawParameterValue ("flutterRate") ->load();
    const float drift         = apvts.getRawParameterValue ("drift")       ->load();
    const bool  syncEnabled   = apvts.getRawParameterValue ("syncEnabled") ->load() > 0.5f;
    const int   divIdx        = static_cast<int> (apvts.getRawParameterValue ("wowDivision")->load());

    wowDepthSm .setTargetValue (apvts.getRawParameterValue ("wowDepth")    ->load());
    flutDepthSm.setTargetValue (apvts.getRawParameterValue ("flutterDepth")->load());
    widthSm    .setTargetValue (apvts.getRawParameterValue ("stereoWidth") ->load());
    mixSm      .setTargetValue (apvts.getRawParameterValue ("dryWet")      ->load());
    warmthSm   .setTargetValue (apvts.getRawParameterValue ("warmth")      ->load());

    // ------------------------------------------------------------------
    // BPM sync: override wowRate when sync is on and host provides tempo
    // ------------------------------------------------------------------
    bool synced = false;
    if (syncEnabled)
    {
        if (auto* playHead = getPlayHead())
        {
            if (auto pos = playHead->getPosition())
            {
                if (pos->getBpm().hasValue())
                {
                    const double bpm = *pos->getBpm();
                    const int safeIdx = juce::jlimit (0, 6, divIdx);
                    wowRate = static_cast<float> (bpm / (60.0 * kDivisionBeats[safeIdx]));
                    synced  = true;
                }
            }
        }
    }

    const int numSamples = buffer.getNumSamples();

    // Tone filters follow warmth once per block; saturation follows it per sample.
    warmthSm.skip (numSamples);
    updateToneFilters (warmthSm.getCurrentValue());
    const float warmth = warmthSm.getCurrentValue();

    // Saturation: asymmetric tanh, normalised to unity gain at low level.
    // Quiet material passes almost untouched; peaks round off.
    const float drive    = 1.0f + 1.5f * warmth;
    const float bias     = 0.12f * warmth;
    const float biasTanh = std::tanh (bias);
    const float satGain  = 1.0f / (drive * (1.0f - biasTanh * biasTanh));
    const float satAmt   = juce::jmin (1.0f, warmth * 2.5f);

    auto saturate = [&] (float x)
    {
        const float s = (std::tanh (drive * x + bias) - biasTanh) * satGain;
        return x + satAmt * (s - x);
    };

    // ------------------------------------------------------------------
    // Phase increments per sample
    // ------------------------------------------------------------------
    const float twoPi = juce::MathConstants<float>::twoPi;
    const float inc0  = twoPi * wowRate          / sr;
    const float inc1  = twoPi * flutRate         / sr;
    const float inc2  = twoPi * kShimmerRateHz   / sr;

    // Drift turns the perfect sines into something a worn transport would
    // produce: wandering wow speed and shape, flutter that swells and catches.
    const float rateWander  = synced ? 0.0f : 0.35f * drift;
    const float wowRandMix  = 0.7f * drift;
    const float swellAmt    = 0.4f * drift;
    const float scrapeAmt   = 0.3f * drift;
    const float msToSmp     = sr / 1000.0f;
    const float maxReadDly  = static_cast<float> (delayL.size()) - 4.0f;
    const float base        = static_cast<float> (baseDelay);

    float* chL = buffer.getWritePointer (0);
    float* chR = buffer.getWritePointer (1);

    float peakDev = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float wowDepthSmp  = wowDepthSm.getNextValue()  * msToSmp;
        const float flutDepthSmp = flutDepthSm.getNextValue() * msToSmp;
        const float shimDepthSmp = flutDepthSmp * kShimmerFrac;
        const float width        = widthSm.getNextValue();
        const float mix          = mixSm.getNextValue();

        // Stereo offset: 0..60 degrees on the sines, plus independent random
        // sources blended in for the right channel.
        const float stereoOffset = width * (juce::MathConstants<float>::pi / 3.0f);

        // Capture pre-advance phases
        const float p0 = phases[0];
        const float p1 = phases[1];
        const float p2 = phases[2];

        const float wander = 1.0f + rateWander * wowRateDrift.process (0.3f);
        phases[0] += inc0 * wander; if (phases[0] >= twoPi) phases[0] -= twoPi;
        phases[1] += inc1;          if (phases[1] >= twoPi) phases[1] -= twoPi;
        phases[2] += inc2;          if (phases[2] >= twoPi) phases[2] -= twoPi;

        const float wowRndL  = wowDrift[0].process (wowRate * 1.3f);
        const float wowRndR  = wowRndL + width * (wowDrift[1].process (wowRate * 1.3f) - wowRndL);
        const float swellL   = flutterSwell[0].process (1.7f);
        const float swellR   = swellL + width * (flutterSwell[1].process (1.7f) - swellL);
        const float scrapeL  = flutterScrape[0].process (flutRate * 2.7f);
        const float scrapeR  = scrapeL + width * (flutterScrape[1].process (flutRate * 2.7f) - scrapeL);

        auto modulation = [&] (float offset, float wowRnd, float swell, float scrape)
        {
            const float wowShape  = std::sin (p0 + offset) + wowRandMix * (wowRnd - std::sin (p0 + offset));
            const float flutShape = std::sin (p1 + offset) * (1.0f + swellAmt * swell) + scrapeAmt * scrape;
            return wowDepthSmp  * wowShape
                 + flutDepthSmp * flutShape
                 + shimDepthSmp * std::sin (p2 + offset);
        };

        const float dlyL = juce::jlimit (1.0f, maxReadDly, base + modulation (0.0f,         wowRndL, swellL, scrapeL));
        const float dlyR = juce::jlimit (1.0f, maxReadDly, base + modulation (stereoOffset, wowRndR, swellR, scrapeR));

        // Read-head speed relative to nominal is 1 - d(delay)/dn.
        peakDev    = juce::jmax (peakDev, std::abs (dlyL - prevDelayL));
        prevDelayL = dlyL;

        delayL.write (chL[i]);
        delayR.write (chR[i]);

        // Dry is read from the same line at the fixed centre delay, so a
        // partial mix blends two time-aligned signals instead of comb filtering.
        const float dryL = delayL.read (base);
        const float dryR = delayR.read (base);
        const float wetL = delayL.read (dlyL);
        const float wetR = delayR.read (dlyR);

        float outL = dryL + mix * (wetL - dryL);
        float outR = dryR + mix * (wetR - dryR);

        // Tape colour on the whole signal.
        outL = hfLoss[0].process (headBump[0].process (dcCut[0].process (saturate (outL))));
        outR = hfLoss[1].process (headBump[1].process (dcCut[1].process (saturate (outR))));

        chL[i] = outL;
        chR[i] = outR;
    }

    speedDeviation.store (peakDev * 100.0f, std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* WowFlutterAudioProcessor::createEditor()
{
    return new WowFlutterAudioProcessorEditor (*this);
}

//==============================================================================
void WowFlutterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void WowFlutterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
//==============================================================================
int WowFlutterAudioProcessor::getNumPrograms()    { return kNumPresets; }
int WowFlutterAudioProcessor::getCurrentProgram() { return currentProgram; }

const juce::String WowFlutterAudioProcessor::getProgramName (int index)
{
    if (index >= 0 && index < kNumPresets)
        return kPresets[index].name;
    return {};
}

void WowFlutterAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= kNumPresets)
        return;

    currentProgram = index;
    const auto& p  = kPresets[index];

    auto setParam = [&] (const juce::String& id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    setParam ("wowRate",      p.wowRate);
    setParam ("flutterRate",  p.flutterRate);
    setParam ("wowDepth",     p.wowDepth);
    setParam ("flutterDepth", p.flutterDepth);
    setParam ("stereoWidth",  p.stereoWidth);
    setParam ("dryWet",       p.dryWet);
    setParam ("warmth",       p.warmth);
    setParam ("drift",        p.drift);
    setParam ("wowDivision",  (float) p.wowDivision);

    if (auto* param = apvts.getParameter ("syncEnabled"))
        param->setValueNotifyingHost (p.syncEnabled ? 1.0f : 0.0f);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WowFlutterAudioProcessor();
}
