#include "PluginEditor.h"

static constexpr int kEditorW = 660;
static constexpr int kEditorH = 376;
static constexpr int kBezel   = 10;     // case thickness around the faceplate
static constexpr int kMargin  = 34;     // left/right content edge, from window edge

static constexpr int kCellW   = 86;     // one knob + legend
static constexpr int kCellH   = 110;
static constexpr int kGroupGap = 26;
static constexpr int kRow0Y   = 92;
static constexpr int kRow1Y   = 238;
static constexpr int kLeftColW = 196;

static constexpr int kGridX   = kEditorW - kMargin - (4 * kCellW + kGroupGap);

//==============================================================================
// Silkscreen group legend: NAME centred, with a bracket line underneath it
// spanning the controls it belongs to.
//==============================================================================
static void drawGroupLegend (juce::Graphics& g, const juce::String& text, float x1, float x2, float y)
{
    const auto  font  = WFFonts::legend (10.5f, true);
    const float textW = juce::GlyphArrangement::getStringWidth (font, text);
    const float mid   = (x1 + x2) * 0.5f;

    g.setColour (juce::Colour (WFColours::kInk));
    g.setFont (font);
    g.drawText (text, juce::Rectangle<float> (mid - textW * 0.5f - 2.0f, y - 8.0f, textW + 4.0f, 16.0f),
                juce::Justification::centred);

    g.drawLine (x1, y, mid - textW * 0.5f - 7.0f, y, 1.0f);
    g.drawLine (mid + textW * 0.5f + 7.0f, y, x2, y, 1.0f);
    g.drawLine (x1 + 0.5f, y, x1 + 0.5f, y + 4.0f, 1.0f);
    g.drawLine (x2 - 0.5f, y, x2 - 0.5f, y + 4.0f, 1.0f);
}

//==============================================================================
void DeviationMeter::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // Bezel
    g.setColour (juce::Colour (WFColours::kInk));
    g.fillRoundedRectangle (b, 4.0f);

    const auto win = b.reduced (5.0f);
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (win.toNearestInt());

    // Paper face, slightly darker toward the top edge where the glass shades it
    g.setGradientFill (juce::ColourGradient (juce::Colour (WFColours::kMeterFace).darker (0.1f), 0.0f, win.getY(),
                                             juce::Colour (WFColours::kMeterFace), 0.0f, win.getY() + win.getHeight() * 0.4f,
                                             false));
    g.fillRect (win);

    const float pivotX = win.getCentreX();
    const float pivotY = win.getBottom() + win.getHeight() * 0.32f;
    const float sweep  = juce::degreesToRadians (45.0f);
    const float radius = juce::jmin (pivotY - win.getY() - 30.0f,
                                     (win.getWidth() * 0.5f - 14.0f) / std::sin (sweep) - 16.0f);

    auto angleFor = [&] (float percent) { return -sweep + 2.0f * sweep * toPos (percent); };
    auto pointAt  = [&] (float angle, float r)
    {
        return juce::Point<float> (pivotX + r * std::sin (angle), pivotY - r * std::cos (angle));
    };

    // Red zone, above 2%
    {
        juce::Path red;
        red.addCentredArc (pivotX, pivotY, radius + 3.0f, radius + 3.0f, 0.0f,
                           angleFor (2.0f), angleFor (5.0f), true);
        g.setColour (juce::Colour (WFColours::kRed));
        g.strokePath (red, juce::PathStrokeType (4.0f));
    }

    // Scale arc and ticks
    g.setColour (juce::Colour (WFColours::kInk));
    {
        juce::Path arc;
        arc.addCentredArc (pivotX, pivotY, radius, radius, 0.0f, -sweep, sweep, true);
        g.strokePath (arc, juce::PathStrokeType (1.0f));
    }

    for (float v : { 0.05f, 0.3f, 0.4f, 0.7f, 1.5f, 3.0f, 4.0f })
    {
        const float a = angleFor (v);
        g.drawLine (juce::Line<float> (pointAt (a, radius), pointAt (a, radius + 4.0f)), 0.8f);
    }

    g.setFont (WFFonts::legend (10.0f));
    const std::pair<float, const char*> majors[] = {
        { 0.0f, "0" }, { 0.1f, ".1" }, { 0.2f, ".2" }, { 0.5f, ".5" }, { 1.0f, "1" }, { 2.0f, "2" }, { 5.0f, "5" } };

    for (const auto& [v, text] : majors)
    {
        const float a = angleFor (v);
        g.drawLine (juce::Line<float> (pointAt (a, radius), pointAt (a, radius + 7.0f)), 1.2f);
        const auto p = pointAt (a, radius + 16.0f);
        g.drawText (text, juce::Rectangle<float> (p.x - 12.0f, p.y - 7.0f, 24.0f, 14.0f),
                    juce::Justification::centred);
    }

    g.setFont (WFFonts::legend (9.0f, true));
    g.setColour (juce::Colour (WFColours::kInkSoft));
    g.drawText ("W & F  %", juce::Rectangle<float> (win.getX(), win.getY() + win.getHeight() * 0.52f,
                                                     win.getWidth(), 14.0f),
                juce::Justification::centred);

    // Needle
    const float a = angleFor (level);
    g.setColour (juce::Colour (0xFF1A1714));
    g.drawLine (juce::Line<float> (pointAt (a, 0.0f), pointAt (a, radius + 8.0f)), 1.3f);

    // Pivot housing
    const float hub = pivotY - win.getBottom() + 12.0f;
    g.setColour (juce::Colour (WFColours::kInk));
    g.fillEllipse (pivotX - hub, pivotY - hub, hub * 2.0f, hub * 2.0f);

    // Glass edge shadow along the top
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRect (win.withHeight (2.0f));
}

//==============================================================================
WowFlutterAudioProcessorEditor::WowFlutterAudioProcessorEditor (WowFlutterAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&laf);

    for (auto* d : { &wowRateDial, &wowDepthDial, &flutterRateDial, &flutterDepthDial,
                     &warmthDial, &driftDial, &widthDial, &mixDial })
        addAndMakeVisible (d);

    addAndMakeVisible (meter);

    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.apvts, "syncEnabled", syncButton);
    addAndMakeVisible (syncButton);

    divisionBox.addItemList ({ "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16" }, 1);

    auto* divParam = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("wowDivision"));
    if (divParam)
        divisionBox.setSelectedItemIndex (divParam->getIndex(), juce::dontSendNotification);

    divisionBox.onChange = [this, divParam]()
    {
        if (divParam)
            divParam->setValueNotifyingHost (
                divParam->convertTo0to1 (static_cast<float> (divisionBox.getSelectedItemIndex())));
    };

    p.apvts.addParameterListener ("wowDivision", this);
    p.apvts.addParameterListener ("syncEnabled", this);
    addAndMakeVisible (divisionBox);

    const bool synced = p.apvts.getRawParameterValue ("syncEnabled")->load() > 0.5f;
    updateSyncState (synced);

    // Fine speckle so the faceplate reads as painted metal rather than a flat fill.
    grain = juce::Image (juce::Image::ARGB, kEditorW, kEditorH, true);
    {
        juce::Random rng (1977);
        juce::Image::BitmapData px (grain, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < kEditorH; ++y)
            for (int x = 0; x < kEditorW; ++x)
            {
                const float n = rng.nextFloat();
                px.setPixelColour (x, y, n < 0.5f ? juce::Colours::black.withAlpha (n * 0.07f)
                                                  : juce::Colours::white.withAlpha ((n - 0.5f) * 0.09f));
            }
    }

    setSize (kEditorW, kEditorH);
}

WowFlutterAudioProcessorEditor::~WowFlutterAudioProcessorEditor()
{
    audioProcessor.apvts.removeParameterListener ("wowDivision", this);
    audioProcessor.apvts.removeParameterListener ("syncEnabled", this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void WowFlutterAudioProcessorEditor::parameterChanged (const juce::String& paramID, float newValue)
{
    juce::Component::SafePointer<WowFlutterAudioProcessorEditor> safe (this);

    if (paramID == "syncEnabled")
    {
        juce::MessageManager::callAsync ([safe, synced = newValue > 0.5f]()
            { if (safe != nullptr) safe->updateSyncState (synced); });
    }
    else if (paramID == "wowDivision")
    {
        juce::MessageManager::callAsync ([safe, idx = static_cast<int> (newValue)]()
            { if (safe != nullptr) safe->divisionBox.setSelectedItemIndex (idx, juce::dontSendNotification); });
    }
}

void WowFlutterAudioProcessorEditor::updateSyncState (bool synced)
{
    wowRateDial.setEnabled (!synced);
    wowRateDial.setAlpha (synced ? 0.4f : 1.0f);
    divisionBox.setEnabled (synced);
    divisionBox.setAlpha (synced ? 1.0f : 0.4f);
}

//==============================================================================
void WowFlutterAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Case
    g.fillAll (juce::Colour (WFColours::kBezel));

    // Faceplate
    const auto face = bounds.reduced ((float) kBezel);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (face.translated (0.0f, 1.5f), 5.0f);
    g.setColour (juce::Colour (WFColours::kFace));
    g.fillRoundedRectangle (face, 5.0f);
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path facePath;
        facePath.addRoundedRectangle (face, 5.0f);
        g.reduceClipRegion (facePath);
        g.drawImageAt (grain, 0, 0);
    }

    // Name plate
    const float left  = (float) kMargin;
    const float right = (float) (kEditorW - kMargin);

    g.setColour (juce::Colour (WFColours::kInk));
    g.setFont (WFFonts::legend (24.0f, true).withExtraKerningFactor (0.0f));
    g.drawText ("Wow & Flutter", juce::Rectangle<float> (left, 22.0f, 300.0f, 30.0f),
                juce::Justification::centredLeft);

    g.setFont (WFFonts::legend (10.0f));
    g.setColour (juce::Colour (WFColours::kInkSoft));
    g.drawText ("TAPE TRANSPORT", juce::Rectangle<float> (right - 200.0f, 30.0f, 200.0f, 18.0f),
                juce::Justification::centredRight);

    g.setColour (juce::Colour (WFColours::kInk));
    g.fillRect (left, 58.0f, right - left, 1.5f);
    g.fillRect (left, 61.5f, right - left, 0.5f);

    // Group legends
    const float legendOffset = 14.0f;
    auto groupSpan = [] (int firstCol) -> std::pair<float, float>
    {
        const float x = (float) (kGridX + firstCol * kCellW + (firstCol >= 2 ? kGroupGap : 0));
        return { x + 6.0f, x + 2.0f * kCellW - 6.0f };
    };

    const auto [wowX1,  wowX2]  = groupSpan (0);
    const auto [flutX1, flutX2] = groupSpan (2);

    drawGroupLegend (g, "WOW",     wowX1,  wowX2,  kRow0Y - legendOffset);
    drawGroupLegend (g, "FLUTTER", flutX1, flutX2, kRow0Y - legendOffset);
    drawGroupLegend (g, "TAPE",    wowX1,  wowX2,  kRow1Y - legendOffset);
    drawGroupLegend (g, "OUTPUT",  flutX1, flutX2, kRow1Y - legendOffset);
    drawGroupLegend (g, "TEMPO",   left,   left + kLeftColW, kRow1Y - legendOffset);
}

//==============================================================================
void WowFlutterAudioProcessorEditor::resized()
{
    LabelledDial* grid[2][4] = {
        { &wowRateDial, &wowDepthDial, &flutterRateDial, &flutterDepthDial },
        { &warmthDial,  &driftDial,    &widthDial,       &mixDial          },
    };

    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 4; ++col)
        {
            const int x = kGridX + col * kCellW + (col >= 2 ? kGroupGap : 0);
            grid[row][col]->setBounds (x, row == 0 ? kRow0Y : kRow1Y, kCellW, kCellH);
        }

    meter.setBounds (kMargin, kRow0Y - 14, kLeftColW, kCellH + 6);

    // Tempo row, centred on the second row of knobs
    const int rowMid = kRow1Y + (kCellH - 18) / 2;
    syncButton .setBounds (kMargin + 4, rowMid - 13, 88, 26);
    divisionBox.setBounds (kMargin + kLeftColW - 92, rowMid - 12, 92, 24);
}
