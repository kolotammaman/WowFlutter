#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Palette — enamel faceplate, printed ink, black skirted knobs.
//==============================================================================
namespace WFColours
{
    static constexpr juce::uint32 kBezel     = 0xFF2A231D;  // case around the faceplate
    static constexpr juce::uint32 kFace      = 0xFFE6DCC6;  // enamel faceplate
    static constexpr juce::uint32 kInk       = 0xFF2B2622;  // silkscreen
    static constexpr juce::uint32 kInkSoft   = 0xFF7A6F62;  // secondary legends
    static constexpr juce::uint32 kKnob      = 0xFF1E1B18;  // knob skirt
    static constexpr juce::uint32 kKnobCap   = 0xFF2D2925;  // knob cap
    static constexpr juce::uint32 kPointer   = 0xFFEDE5D3;  // knob line
    static constexpr juce::uint32 kRed       = 0xFFB0402E;  // LED, meter red zone
    static constexpr juce::uint32 kMeterFace = 0xFFF2E5C4;  // meter paper
}

namespace WFFonts
{
    inline juce::Font legend (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Futura", size,
                                              bold ? juce::Font::bold : juce::Font::plain))
                   .withExtraKerningFactor (0.06f);
    }
}

//==============================================================================
class WowFlutterLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WowFlutterLookAndFeel()
    {
        setColour (juce::ComboBox::textColourId,                   juce::Colour (WFColours::kInk));
        setColour (juce::ComboBox::backgroundColourId,             juce::Colour (WFColours::kFace));
        setColour (juce::ComboBox::outlineColourId,                juce::Colour (WFColours::kInk));
        setColour (juce::ComboBox::arrowColourId,                  juce::Colour (WFColours::kInk));
        setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (WFColours::kFace));
        setColour (juce::PopupMenu::textColourId,                  juce::Colour (WFColours::kInk));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (WFColours::kInk));
        setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colour (WFColours::kFace));
    }

    // Skirted knob with a printed 0–10 scale on the panel around it.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override
    {
        const auto  b      = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float cx     = b.getCentreX();
        const float cy     = b.getCentreY();
        const float scaleR = juce::jmin (width, height) * 0.5f - 2.0f;
        const float skirtR = scaleR - 9.0f;
        const float capR   = skirtR * 0.74f;
        const float angle  = startAngle + sliderPos * (endAngle - startAngle);

        // Scale ticks
        g.setColour (juce::Colour (WFColours::kInk));
        for (int i = 0; i <= 10; ++i)
        {
            const float a     = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            const bool  major = (i % 5 == 0);
            const float r0    = scaleR - (major ? 6.0f : 4.0f);
            g.drawLine (cx + r0 * std::sin (a),     cy - r0 * std::cos (a),
                        cx + scaleR * std::sin (a), cy - scaleR * std::cos (a),
                        major ? 1.4f : 0.9f);
        }

        // Skirt, with a soft contact shadow on the panel
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillEllipse (cx - skirtR + 1.0f, cy - skirtR + 2.5f, skirtR * 2.0f, skirtR * 2.0f);
        g.setColour (juce::Colour (WFColours::kKnob));
        g.fillEllipse (cx - skirtR, cy - skirtR, skirtR * 2.0f, skirtR * 2.0f);

        // Knurling on the cap edge
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        for (int i = 0; i < 36; ++i)
        {
            const float a = angle + juce::MathConstants<float>::twoPi * (float) i / 36.0f;
            g.drawLine (cx + capR * 0.9f * std::sin (a), cy - capR * 0.9f * std::cos (a),
                        cx + capR * std::sin (a),        cy - capR * std::cos (a), 1.0f);
        }

        // Cap: flat, lit faintly from above
        juce::ColourGradient cap (juce::Colour (WFColours::kKnobCap).brighter (0.12f), cx, cy - capR,
                                  juce::Colour (WFColours::kKnobCap),                  cx, cy + capR, false);
        g.setGradientFill (cap);
        g.fillEllipse (cx - capR * 0.9f, cy - capR * 0.9f, capR * 1.8f, capR * 1.8f);

        // Line indicator running across skirt and cap
        g.setColour (juce::Colour (WFColours::kPointer));
        g.drawLine (cx + capR * 0.25f * std::sin (angle), cy - capR * 0.25f * std::cos (angle),
                    cx + (skirtR - 1.5f) * std::sin (angle), cy - (skirtR - 1.5f) * std::cos (angle),
                    2.0f);
    }

    // Small push button with an LED beside it.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn,
                           bool isMouseOver, bool isButtonDown) override
    {
        const bool  on  = btn.getToggleState();
        const auto  b   = btn.getLocalBounds().toFloat();
        const float led = 8.0f;
        const float ly  = b.getCentreY() - led * 0.5f;

        g.setColour (juce::Colour (WFColours::kInk));
        g.fillEllipse (1.0f, ly - 1.0f, led + 2.0f, led + 2.0f);
        g.setColour (on ? juce::Colour (WFColours::kRed).brighter (0.25f)
                        : juce::Colour (0xFF4A2A24));
        g.fillEllipse (2.0f, ly, led, led);

        auto cap = juce::Rectangle<float> (led + 10.0f, b.getCentreY() - 10.0f, 22.0f, 20.0f);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (cap.translated (0.0f, isButtonDown ? 0.5f : 1.5f), 2.5f);
        g.setColour (juce::Colour (WFColours::kKnob).brighter (isMouseOver ? 0.15f : 0.0f));
        g.fillRoundedRectangle (cap.translated (0.0f, isButtonDown ? 1.0f : 0.0f), 2.5f);

        g.setColour (juce::Colour (WFColours::kInk));
        g.setFont (WFFonts::legend (11.0f, true));
        g.drawText (btn.getButtonText(),
                    cap.withX (cap.getRight() + 8.0f).withRight (b.getRight()),
                    juce::Justification::centredLeft);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox&) override
    {
        const auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        g.setColour (juce::Colour (WFColours::kInk));
        g.drawRect (b, 1.0f);

        juce::Path tri;
        const float ax = (float) width - 12.0f, ay = height * 0.5f;
        tri.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.fillPath (tri);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 0, box.getWidth() - 24, box.getHeight());
        label.setFont (WFFonts::legend (12.0f));
    }

    juce::Font getPopupMenuFont() override { return WFFonts::legend (13.0f); }
};

//==============================================================================
// Knob with its legend printed underneath. The legend shows the value while
// the knob is hovered or turned, and the name otherwise.
//==============================================================================
struct LabelledDial : public juce::Component
{
    LabelledDial (const juce::String& labelText,
                  juce::AudioProcessorValueTreeState& apvts,
                  const juce::String& paramID,
                  juce::LookAndFeel& laf)
        : name (labelText)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::degreesToRadians (-135.0f),
                                    juce::degreesToRadians (135.0f), true);
        slider.setLookAndFeel (&laf);
        slider.onHover       = [this] (bool h) { hovered = h; refreshLegend(); };
        slider.onValueChange = [this] { refreshLegend(); };
        slider.onDragEnd     = [this] { refreshLegend(); };
        addAndMakeVisible (slider);

        legend.setJustificationType (juce::Justification::centred);
        legend.setFont (WFFonts::legend (11.0f, true));
        legend.setColour (juce::Label::textColourId, juce::Colour (WFColours::kInk));
        legend.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (legend);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, paramID, slider);
        refreshLegend();
    }

    ~LabelledDial() override { slider.setLookAndFeel (nullptr); }

    void resized() override
    {
        auto area = getLocalBounds();
        legend.setBounds (area.removeFromBottom (18));
        slider.setBounds (area);
    }

    struct KnobSlider : public juce::Slider
    {
        std::function<void (bool)> onHover;
        void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); if (onHover) onHover (true); }
        void mouseExit  (const juce::MouseEvent& e) override { juce::Slider::mouseExit  (e); if (onHover) onHover (false); }
    };

    KnobSlider  slider;
    juce::Label legend;

private:
    void refreshLegend()
    {
        const bool showValue = hovered || slider.isMouseButtonDown();
        legend.setText (showValue ? slider.getTextFromValue (slider.getValue()) : name,
                        juce::dontSendNotification);
        legend.setColour (juce::Label::textColourId,
                          juce::Colour (showValue ? WFColours::kRed : WFColours::kInk));
    }

    juce::String name;
    bool hovered = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
// Needle meter showing peak speed deviation, the way a W&F test set would.
//==============================================================================
class DeviationMeter : public juce::Component,
                       private juce::Timer
{
public:
    explicit DeviationMeter (std::atomic<float>& source) : deviation (source)
    {
        startTimerHz (30);
    }

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override
    {
        const float target = deviation.load (std::memory_order_relaxed);
        level += (target - level) * (target > level ? 0.45f : 0.12f);
        repaint();
    }

    // Square-root scale: room to read 0.1% while still reaching 5%.
    static float toPos (float percent) { return std::sqrt (juce::jlimit (0.0f, 5.0f, percent) / 5.0f); }

    std::atomic<float>& deviation;
    float level = 0.0f;
};

//==============================================================================
class WowFlutterAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit WowFlutterAudioProcessorEditor (WowFlutterAudioProcessor&);
    ~WowFlutterAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    void parameterChanged (const juce::String& paramID, float newValue) override;
    void updateSyncState  (bool synced);

    WowFlutterAudioProcessor& audioProcessor;
    WowFlutterLookAndFeel laf;

    LabelledDial wowRateDial     { "RATE",   audioProcessor.apvts, "wowRate",      laf };
    LabelledDial wowDepthDial    { "DEPTH",  audioProcessor.apvts, "wowDepth",     laf };
    LabelledDial flutterRateDial { "RATE",   audioProcessor.apvts, "flutterRate",  laf };
    LabelledDial flutterDepthDial{ "DEPTH",  audioProcessor.apvts, "flutterDepth", laf };
    LabelledDial warmthDial      { "WARMTH", audioProcessor.apvts, "warmth",       laf };
    LabelledDial driftDial       { "DRIFT",  audioProcessor.apvts, "drift",        laf };
    LabelledDial widthDial       { "WIDTH",  audioProcessor.apvts, "stereoWidth",  laf };
    LabelledDial mixDial         { "MIX",    audioProcessor.apvts, "dryWet",       laf };

    DeviationMeter meter { audioProcessor.speedDeviation };

    juce::ToggleButton syncButton { "SYNC" };
    juce::ComboBox     divisionBox;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;

    juce::Image grain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WowFlutterAudioProcessorEditor)
};
