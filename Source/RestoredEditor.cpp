#include "RestoredEditor.h"
#include "CrashTrace.h"
#include "Theme.h"
#include "dsp/BiquadCascade.h"
#include <BinaryData.h>
#include <cmath>
#include <complex>
#include <algorithm>

namespace
{
// Native compact layout: 60% of the original 1784x882 artwork footprint.
// Paint uses these readable compact-design coordinates instead of shrinking
// the 1784px text and controls to illegible sizes.
constexpr float baseW = 1070.0f;
constexpr float baseH = 529.0f;
const juce::Colour green(0xff39ff3a), bright(0xff98ff8c), dark(0xff061008);

// Load by named-resource lookup: avoids platform dependent BinaryData identifiers.
// Each resource is decoded exactly once when the editor is created.
juce::Image loadArtwork(const juce::String& prefix)
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const auto name = juce::String(BinaryData::namedResourceList[i]);
        if (!name.startsWithIgnoreCase(prefix)) continue;
        int length = 0;
        if (const char* bytes = BinaryData::getNamedResource(name.toRawUTF8(), length))
            if (length > 0)
                return juce::ImageFileFormat::loadFrom(bytes, static_cast<size_t>(length));
    }
    return {};
}

// Paint clean panels over a separate empty frame. Never use a screenshot of
// existing controls as the background: that caused v1.5's doubled interface.
void panel(juce::Graphics& g, juce::Rectangle<float> r, float corner = 8.f)
{
    // Panels are lightweight JUCE vectors, never baked text or interactive art.
    juce::ColourGradient surface(juce::Colour(0xff0c1910), r.getX(), r.getY(),
                                 juce::Colour(0xff020805), r.getX(), r.getBottom(), false);
    g.setGradientFill(surface);
    g.fillRoundedRectangle(r, corner);
    g.setColour(juce::Colour(0xff267b36).withAlpha(.64f));
    g.drawRoundedRectangle(r.reduced(.5f), corner, 1.0f);
    g.setColour(juce::Colour(0xffa9ffc0).withAlpha(.13f));
    g.drawLine(r.getX() + 12.f, r.getY() + 1.f, r.getRight() - 12.f, r.getY() + 1.f, 1.f);
}

void separator(juce::Graphics& g, float x1, float x2, float y)
{
    g.setColour(juce::Colour(0xff59ff68).withAlpha(.16f));
    g.drawLine(x1, y, x2, y, 1.f);
}

void configureDial(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 92, 19);
    s.setDoubleClickReturnValue(true, 0.0);
    s.setColour(juce::Slider::textBoxTextColourId, bright);
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff071109));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff235d2b));
    s.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}
// JUCE parameter attachments configure their Slider and can overwrite custom
// readout settings. Re-apply these AFTER creating/rebinding attachments.
void formatFrequency(juce::Slider& s)
{
    s.textFromValueFunction = [](double v)
    {
        if (!std::isfinite(v)) return juce::String("-- Hz");
        return v >= 1000.0 ? juce::String(v / 1000.0, v >= 10000.0 ? 1 : 2) + " kHz"
                           : juce::String(v, v < 100.0 ? 1 : 0) + " Hz";
    };
    s.valueFromTextFunction = [](const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        return t.getDoubleValue() * (t.contains("khz") || t.endsWithChar('k') ? 1000.0 : 1.0);
    };
    s.updateText();
}
void formatDecibels(juce::Slider& s)
{
    s.textFromValueFunction = [](double v)
    {
        if (!std::isfinite(v)) return juce::String("-- dB");
        return juce::String(v > 0.05 ? "+" : "") + juce::String(v, 1) + " dB";
    };
    s.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
    s.updateText();
}
void formatQ(juce::Slider& s)
{
    s.textFromValueFunction = [](double v)
    {
        return std::isfinite(v) ? juce::String(v, 2) : juce::String("--");
    };
    s.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
    s.updateText();
}
void formatPercent(juce::Slider& s)
{
    s.textFromValueFunction = [](double v) { return juce::String(v, 0) + "%"; };
    s.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
    s.updateText();
}

void configureChoice(juce::ComboBox& c, const juce::StringArray& choices)
{
    c.addItemList(choices, 1);
    c.setColour(juce::ComboBox::backgroundColourId, dark);
    c.setColour(juce::ComboBox::textColourId, bright);
    c.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2b8e34));
}
void label(juce::Graphics& g, const juce::String& text, float x, float y, float w, float h,
           float size = 16.f, juce::Colour colour = green)
{
    g.setColour(colour);
    g.setFont(juce::Font(juce::FontOptions(size).withStyle("Bold")));
    g.drawFittedText(text, juce::Rectangle<float>(x, y, w, h).toNearestInt(),
                     juce::Justification::centred, 1);
}
} // namespace

SkellyEQRestoredEditor::NeonLook::NeonLook()
{
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff08120c));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff195b27));
    setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc4ebcb));
    setColour(juce::TextButton::textColourOnId, juce::Colour(0xffecfff0));
    setColour(juce::ToggleButton::textColourId, juce::Colour(0xffc4ebcb));
}

void SkellyEQRestoredEditor::NeonLook::drawButtonBackground(juce::Graphics& g, juce::Button& button,
    const juce::Colour&, bool hover, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(1.2f);
    const bool active = button.getToggleState();
    const bool isBandTab = button.getName() == "SkellyBandTab";
    const auto base = isBandTab ? (active ? juce::Colour(0xff60411b)
                                         : button.findColour(juce::TextButton::buttonColourId))
                                : juce::Colour(active ? 0xff1a5b24 : 0xff09180d);
    g.setGradientFill(juce::ColourGradient(base.brighter(hover ? .23f : .10f), 0.f, 0.f,
                         base.darker(down ? .24f : .05f), 0.f, bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, 4.f);
    const auto stroke = isBandTab && active ? juce::Colour(0xffffc571)
                       : active ? juce::Colour(0xff75ff86)
                                : juce::Colour(hover ? 0xff45d954 : 0xff2b6236);
    g.setColour(stroke.withAlpha(button.isEnabled() ? .90f : .35f));
    g.drawRoundedRectangle(bounds, 4.f, active ? 1.6f : 1.f);
    if (active)
    {
        g.setColour(juce::Colour(0xff7cff82).withAlpha(.15f));
        g.fillRoundedRectangle(bounds.reduced(3.f), 3.f);
    }
}

void SkellyEQRestoredEditor::NeonLook::drawButtonText(juce::Graphics& g, juce::TextButton& button,
    bool, bool)
{
    const auto r = button.getLocalBounds().reduced(3, 1);
    const float size = juce::jlimit(10.f, 12.5f, button.getHeight() * .52f);
    g.setFont(juce::Font(juce::FontOptions(size).withStyle("Bold")));
    g.setColour(juce::Colour(button.isEnabled()
        ? (button.getName() == "SkellyBandTab" && button.getToggleState() ? 0xffffe0a0
            : button.getToggleState() ? 0xfff1fff3 : 0xffbceac7)
        : 0xff667d6b));
    g.drawFittedText(button.getButtonText(), r, juce::Justification::centred, 1);
}

void SkellyEQRestoredEditor::NeonLook::drawComboBox(juce::Graphics& g, int width, int height,
    bool, int, int, int, int, juce::ComboBox& combo)
{
    const auto r = juce::Rectangle<float>(.8f, .8f, (float)width - 1.6f, (float)height - 1.6f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff122317), 0.f, 0.f,
                      juce::Colour(0xff06100a), 0.f, (float)height, false));
    g.fillRoundedRectangle(r, 5.f);
    g.setColour(juce::Colour(0xff347a3b).withAlpha(combo.isEnabled() ? .85f : .30f));
    g.drawRoundedRectangle(r, 5.f, 1.f);
    const float cx = (float)width - 17.f, cy = height * .52f;
    juce::Path arrow;
    arrow.startNewSubPath(cx - 5.f, cy - 2.f);
    arrow.lineTo(cx, cy + 3.f);
    arrow.lineTo(cx + 5.f, cy - 2.f);
    g.setColour(juce::Colour(0xffb4fdbb));
    g.strokePath(arrow, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

juce::Font SkellyEQRestoredEditor::NeonLook::getComboBoxFont(juce::ComboBox& box)
{
    return juce::Font(juce::FontOptions(juce::jlimit(10.f, 12.0f,
                                                 box.getHeight() * .54f)).withStyle("Bold"));
}

void SkellyEQRestoredEditor::NeonLook::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
    bool hover, bool)
{
    const auto r = button.getLocalBounds().toFloat().reduced(1.f);
    const bool active = button.getToggleState();
    g.setColour(juce::Colour(active ? 0xff184c22 : (hover ? 0xff172c1c : 0xff0b1a0e)));
    g.fillRoundedRectangle(r, 6.f);
    g.setColour(juce::Colour(active ? 0xff72ff82 : 0xff357347));
    g.drawRoundedRectangle(r, 6.f, active ? 1.6f : 1.f);
    const float cy = r.getCentreY();
    g.setColour(juce::Colour(active ? 0xff8fff96 : 0xff445d49));
    g.fillEllipse(r.getX() + 11.f, cy - 4.f, 8.f, 8.f);
    g.setColour(juce::Colour(0xffe0fbe4));
    g.setFont(juce::Font(juce::FontOptions(juce::jlimit(9.f, 14.f, r.getHeight() * .42f))
                        .withStyle("Bold")));
    g.drawFittedText(button.getButtonText(),
                     juce::Rectangle<float>(r.getX() + 24.f, r.getY(), r.getWidth() - 29.f, r.getHeight()).toNearestInt(),
                     juce::Justification::centred, 1);
}

void SkellyEQRestoredEditor::NeonLook::drawLinearSlider(juce::Graphics& g, int x, int y,
    int width, int height, float sliderPos, float, float, juce::Slider::SliderStyle style, juce::Slider&)
{
    if (style != juce::Slider::LinearHorizontal)
        return;
    const float mid = y + height * .5f;
    const float left = x + 6.f;
    const float right = x + width - 6.f;
    const float knobX = juce::jlimit(left, right, sliderPos);
    g.setColour(juce::Colour(0xff23472c));
    g.fillRoundedRectangle(left, mid - 2.f, right - left, 4.f, 2.f);
    g.setColour(juce::Colour(0xff53ff66));
    g.fillRoundedRectangle(left, mid - 2.f, juce::jmax(1.f, knobX - left), 4.f, 2.f);
    g.setColour(juce::Colour(0xffd4ffda));
    g.fillEllipse(knobX - 5.5f, mid - 5.5f, 11.f, 11.f);
}

void SkellyEQRestoredEditor::NeonLook::drawRotarySlider(juce::Graphics& g, int x, int y,
    int width, int height, float proportion, float startAngle, float endAngle, juce::Slider&)
{
    const float radius = .41f * (float)juce::jmin(width, height);
    const float cx = x + width * .5f, cy = y + height * .5f;
    const float a = startAngle + proportion * (endAngle - startAngle);
    const float ring = radius + 4.f;
    g.setColour(juce::Colour(0xff0b130e));
    g.fillEllipse(cx - ring - 4.f, cy - ring - 4.f, 2.f * (ring + 4.f), 2.f * (ring + 4.f));
    juce::Path track;
    track.addCentredArc(cx, cy, ring, ring, 0.f, startAngle, endAngle, true);
    g.setColour(juce::Colour(0xff286837));
    g.strokePath(track, juce::PathStrokeType(4.f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
    juce::Path activeArc;
    activeArc.addCentredArc(cx, cy, ring, ring, 0.f, startAngle, a, true);
    g.setColour(juce::Colour(0xff45ff58).withAlpha(.18f));
    g.strokePath(activeArc, juce::PathStrokeType(8.f));
    g.setColour(juce::Colour(0xff71ff83));
    g.strokePath(activeArc, juce::PathStrokeType(3.f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b4540), cx - radius, cy - radius,
                                         juce::Colour(0xff07120b), cx + radius, cy + radius, false));
    g.fillEllipse(cx - radius, cy - radius, 2.f * radius, 2.f * radius);
    g.setColour(juce::Colour(0xff7a9a7d).withAlpha(.85f));
    g.drawEllipse(cx - radius, cy - radius, 2.f * radius, 2.f * radius, 1.3f);
    g.setColour(juce::Colour(0xff0a150d));
    g.fillEllipse(cx - radius * .72f, cy - radius * .72f, 1.44f * radius, 1.44f * radius);
    juce::Path pointer;
    pointer.startNewSubPath(cx, cy);
    pointer.lineTo(cx + std::sin(a) * radius * .68f,
                   cy - std::cos(a) * radius * .68f);
    g.setColour(juce::Colour(0xffaaffb6));
    g.strokePath(pointer, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    g.setColour(juce::Colour(0xffa7ffb2));
    g.fillEllipse(cx - 2.f, cy - 2.f, 4.f, 4.f);
}

juce::String SkellyEQRestoredEditor::bandId(int band, const char* name)
{
    return "b" + juce::String(band + 1) + "_" + name;
}

SkellyEQRestoredEditor::ResponseGraph::ResponseGraph(SkellyEQAudioProcessor& p, int& s)
    : processor(p), selected(s)
{
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    // Decode once on the message thread; never in paint or processBlock.
    graphTexture = loadArtwork("graph_bg");
    spectrumDb.fill(-90.f);
}

juce::RangedAudioParameter* SkellyEQRestoredEditor::ResponseGraph::param(int b, const char* n) const
{
    return processor.state.getParameter(SkellyEQRestoredEditor::bandId(b, n));
}
void SkellyEQRestoredEditor::ResponseGraph::setParam(int b, const char* n, float value)
{
    if (auto* p = param(b, n))
    {
        // Dragged parameter has one continuous host gesture. Other changes
        // (e.g. gain while frequency is dragged, wheel Q) have valid gestures.
        const bool ownGesture = p != gestureParam && p != gainGestureParam;
        if (ownGesture) p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        if (ownGesture) p->endChangeGesture();
    }
    repaint();
}
float SkellyEQRestoredEditor::ResponseGraph::xForFrequency(float f) const
{
    const float usable = (float)juce::jmax(1, getWidth() - 28);
    return 14.f + std::log(juce::jlimit(20.f, 20000.f, f) / 20.f) / std::log(1000.f) * usable;
}
float SkellyEQRestoredEditor::ResponseGraph::frequencyForX(float x) const
{
    return 20.f * std::pow(1000.f, juce::jlimit(0.f, 1.f,
        (x - 14.f) / (float)juce::jmax(1, getWidth() - 28)));
}
float SkellyEQRestoredEditor::ResponseGraph::yForGain(float g) const
{
    const int ranges[] = {6, 12, 18, 30};
    const int index = juce::jlimit(0, 3, (int)processor.state.getRawParameterValue("graphRange")->load());
    const float range = (float)ranges[index];
    return (range - g) / (2.f * range) * (float)getHeight();
}
float SkellyEQRestoredEditor::ResponseGraph::gainForY(float y) const
{
    const int ranges[] = {6, 12, 18, 30};
    const int index = juce::jlimit(0, 3, (int)processor.state.getRawParameterValue("graphRange")->load());
    const float range = (float)ranges[index];
    return juce::jlimit(-18.f, 18.f, range * (1.f - 2.f * y / (float)juce::jmax(1, getHeight())));
}
int SkellyEQRestoredEditor::ResponseGraph::findBand(juce::Point<float> position) const
{
    const auto bands = processor.bands();
    // Overlapping nodes: prefer the selected node, otherwise choose the nearest.
    // This also keeps bypassed nodes reachable after loading old FL projects.
    int closest = -1;
    float bestDistance = 19.f;
    for (int b = 0; b < 8; ++b)
    {
        const auto& band = bands[(size_t)b];
        const auto target = juce::Point<float>(
            xForFrequency((float)band.freq),
            juce::jlimit(9.f, (float)getHeight() - 9.f, yForGain(SkellyDSP::filterUsesGain(band.type) ? (float)band.gain : 0.f)));
        const float distance = position.getDistanceFrom(target);
        if (b == selected && distance <= 15.f) return b;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            closest = b;
        }
    }
    return closest;
}
void SkellyEQRestoredEditor::ResponseGraph::beginDragParam(juce::RangedAudioParameter* p)
{
    if (gestureParam == p) return;
    if (gestureParam != nullptr) gestureParam->endChangeGesture();
    gestureParam = p;
    if (gestureParam != nullptr) gestureParam->beginChangeGesture();
}
void SkellyEQRestoredEditor::ResponseGraph::endDragParam()
{
    if (gestureParam != nullptr)
    {
        gestureParam->endChangeGesture();
        gestureParam = nullptr;
    }
    if (gainGestureParam != nullptr)
    {
        gainGestureParam->endChangeGesture();
        gainGestureParam = nullptr;
    }
}

void SkellyEQRestoredEditor::ResponseGraph::paint(juce::Graphics& g)
{
    const float w = (float)getWidth(), h = (float)getHeight();
    if (w < 2.f || h < 2.f) return;
    g.fillAll(juce::Colour(0xff030a05));
    if (graphTexture.isValid())
    {
        g.setOpacity(0.12f);
        g.drawImage(graphTexture, 0, 0, w, h, 0, 0,
                    graphTexture.getWidth(), graphTexture.getHeight());
        g.setOpacity(1.f);
    }
    const int ranges[] = {6, 12, 18, 30};
    const int range = ranges[juce::jlimit(0, 3,
        (int)processor.state.getRawParameterValue("graphRange")->load())];
    for (int db = -range; db <= range; db += (range <= 18 ? (range == 6 ? 2 : 6) : 10))
    {
        const float y = yForGain((float)db);
        g.setColour(green.withAlpha(db == 0 ? .42f : .18f));
        g.drawHorizontalLine((int)y, 0.f, w);
    }
    for (int decade = 1; decade <= 10000; decade *= 10)
        for (int n = 1; n <= 9; ++n)
        {
            int frequency = decade * n;
            if (frequency < 20 || frequency > 20000) continue;
            g.setColour(green.withAlpha((n == 1 || n == 2 || n == 5) ? .24f : .09f));
            g.drawVerticalLine((int)xForFrequency((float)frequency), 0.f, h);
        }
    // FFT data is computed by the editor timer, never inside audio callbacks.
    // Display only; cannot touch processor or filter state.
    if (hasSpectrum && spectrumOpacity > 0.001f)
    {
        juce::Path spectrumPath;
        for (int k = 0; k < (int)spectrumDb.size(); ++k)
        {
            const float x = xForFrequency(20.f * std::pow(1000.f, (float)k / 255.f));
            // -90..0 dB spectrum mapped to lower 88% of the graph.
            const float normalized = juce::jlimit(0.f, 1.f, (spectrumDb[(size_t)k] + 90.f) / 90.f);
            const float y = h * (0.94f - normalized * 0.82f);
            if (k == 0) spectrumPath.startNewSubPath(x, y);
            else spectrumPath.lineTo(x, y);
        }
        juce::Path fill(spectrumPath);
        fill.lineTo(w, h);
        fill.lineTo(0.f, h);
        fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff38fa46).withAlpha(.27f * spectrumOpacity), 0.f, h * .35f,
                                              juce::Colour(0xff38fa46).withAlpha(.01f * spectrumOpacity), 0.f, h, false));
        g.fillPath(fill);
        g.setColour(juce::Colour(0xff70ff70).withAlpha(.55f * spectrumOpacity));
        g.strokePath(spectrumPath, juce::PathStrokeType(1.2f));
    }
    const auto bands = processor.bands();
    const int slope = 1 + (int)processor.state.getRawParameterValue("slope")->load();
    const int stereoMode = (int)processor.state.getRawParameterValue("stereoMode")->load();
    bool curveChanged = !curveValid || cachedSlope != slope || cachedRange != range
                        || cachedMode != stereoMode
                        || cachedWidth != getWidth() || cachedHeight != getHeight()
                        || cachedRate != processor.sampleRateHz();
    if (!curveChanged)
        for (int b = 0; b < 8 && !curveChanged; ++b)
        {
            const auto& a = bands[(size_t)b];
            const auto& old = cachedBands[(size_t)b];
            curveChanged = a.enabled != old.enabled || a.solo != old.solo
                        || a.type != old.type || a.channel != old.channel
                        || a.freq != old.freq || a.gain != old.gain || a.q != old.q;
        }
    if (curveChanged)
    {
        cachedResponse.clear();
        cachedLaneA.clear();
        cachedLaneB.clear();
        lanesDiffer = false;
        bool anySolo = false;
        for (const auto& band : bands) anySolo |= band.enabled && band.solo;
        const double sr = juce::jmax(8000.0, processor.sampleRateHz());
        const double limit = juce::jmin(20000.0, sr * 0.495);
        for (int i = 0; i < 550; ++i)
        {
            const float x = w * (float)i / 549.f;
            const double frequency = juce::jmin((double)frequencyForX(x), limit);
            std::complex<double> lane[2] {{1.0, 0.0}, {1.0, 0.0}};
            for (const auto& band : bands)
            {
                if (!band.enabled || (anySolo && !band.solo)) continue;
                const auto response = SkellyDSP::bandResponse(band, frequency, sr, slope);
                for (int ch = 0; ch < 2; ++ch)
                    if (SkellyDSP::routesToLane(band, stereoMode, ch))
                        lane[ch] *= response;
            }
            const float dbA = (float)juce::Decibels::gainToDecibels(std::abs(lane[0]), -100.0);
            const float dbB = (float)juce::Decibels::gainToDecibels(std::abs(lane[1]), -100.0);
            const float yA = juce::jlimit(-2.f * h, 3.f * h, yForGain(dbA));
            const float yB = juce::jlimit(-2.f * h, 3.f * h, yForGain(dbB));
            const float yMean = juce::jlimit(-2.f * h, 3.f * h, yForGain((dbA + dbB) * .5f));
            if (std::abs(dbA - dbB) > .25f) lanesDiffer = true;
            if (i == 0)
            {
                cachedResponse.startNewSubPath(x, yMean);
                cachedLaneA.startNewSubPath(x, yA);
                cachedLaneB.startNewSubPath(x, yB);
            }
            else
            {
                cachedResponse.lineTo(x, yMean);
                cachedLaneA.lineTo(x, yA);
                cachedLaneB.lineTo(x, yB);
            }
        }
        cachedBands = bands;
        cachedSlope = slope;
        cachedRange = range;
        cachedMode = stereoMode;
        cachedWidth = getWidth();
        cachedHeight = getHeight();
        cachedRate = processor.sampleRateHz();
        curveValid = true;
    }
    // Soft response fill gives boosts and cuts useful visual weight without
    // obscuring the FFT or the zero line.
    {
        juce::Path responseFill(cachedResponse);
        const float zeroY = yForGain(0.f);
        responseFill.lineTo(w, zeroY);
        responseFill.lineTo(0.f, zeroY);
        responseFill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(green.withAlpha(.16f), 0.f, zeroY - h * .28f,
                                              green.withAlpha(.015f), 0.f, h, false));
        g.fillPath(responseFill);
    }
    // Crosshair follows the selected band's frequency and gain.
    {
        const auto& band = bands[(size_t)juce::jlimit(0, 7, selected)];
        const float nodeX = xForFrequency((float)band.freq);
        g.setColour(green.withAlpha(.13f));
        g.drawVerticalLine((int)nodeX, 0.f, h);
    }
    // Two discrete lane responses appear only when routing actually differs.
    // The luminous centre line is the dB-mean (L/R, or M/S in mid-side mode).
    if (lanesDiffer)
    {
        g.setColour(juce::Colour(0xffffc976).withAlpha(.64f));
        g.strokePath(cachedLaneA, juce::PathStrokeType(1.15f));
        g.setColour(juce::Colour(0xff69dce3).withAlpha(.64f));
        g.strokePath(cachedLaneB, juce::PathStrokeType(1.15f));
    }
    g.setColour(green.withAlpha(.12f));
    g.strokePath(cachedResponse, juce::PathStrokeType(8.f));
    g.setColour(bright.withAlpha(.38f));
    g.strokePath(cachedResponse, juce::PathStrokeType(4.f));
    g.setColour(green);
    g.strokePath(cachedResponse, juce::PathStrokeType(2.2f));
    if (lanesDiffer)
    {
        g.setFont(juce::Font(juce::FontOptions(10.f).withStyle("Bold")));
        g.setColour(juce::Colour(0xffffc976));
        g.drawText(stereoMode == 0 ? "L" : "M", 8, 5, 13, 15, juce::Justification::centred);
        g.setColour(juce::Colour(0xff69dce3));
        g.drawText(stereoMode == 0 ? "R" : "S", 24, 5, 13, 15, juce::Justification::centred);
    }
    // The frequency axis stays 20 Hz..20 kHz. Mark unavailable frequencies
    // at low sample rates instead of displaying aliased curves as real DSP.
    const double nyquist = processor.sampleRateHz() * .5;
    if (nyquist < 20000.0)
    {
        const float x = xForFrequency((float)juce::jmax(20.0, nyquist));
        g.setColour(juce::Colour(0xff020805).withAlpha(.82f));
        g.fillRect(juce::Rectangle<float>(x, 0.f, w - x, h));
        g.setColour(juce::Colour(0xffffc976).withAlpha(.65f));
        g.drawVerticalLine((int)x, 0.f, h);
        g.setFont(juce::Font(juce::FontOptions(10.f)));
        g.drawFittedText("NYQUIST", juce::Rectangle<float>(x + 4.f, 6.f, w - x - 7.f, 15.f).toNearestInt(),
                         juce::Justification::centred, 1);
    }
    bool anyEnabled = false;
    for (int pass = 0; pass < 2; ++pass)
    for (int b = 0; b < 8; ++b)
    {
        if ((b == selected) != (pass == 1)) continue;
        const auto& band = bands[(size_t)b];
        anyEnabled |= band.enabled;
        const float x = xForFrequency((float)band.freq);
        // Keep handles reachable when a saved gain exceeds the display range.
        const float y = juce::jlimit(9.f, h-9.f,
            yForGain(SkellyDSP::filterUsesGain(band.type) ? (float)band.gain : 0.f));
        // Dim ring = bypassed, bright ring = active, amber = selected.
        // Always render saved band positions, even if all bands are disabled.
        if (band.enabled || b == selected)
        {
            g.setColour((b == selected ? juce::Colour(0xffffc21a) : green).withAlpha(.13f));
            g.fillEllipse(x - 18.f, y - 18.f, 36.f, 36.f);
        }
        g.setColour(b == selected ? juce::Colour(0xffffc21a)
                    : band.enabled ? bright : green.withAlpha(.38f));
        g.drawEllipse(x - 9.f, y - 9.f, 18.f, 18.f, 2.4f);
        g.setColour(dark);
        g.fillEllipse(x - 5.f, y - 5.f, 10.f, 10.f);
        if (band.enabled)
        {
            g.setColour(green);
            g.fillEllipse(x - 2.f, y - 2.f, 4.f, 4.f);
        }
        // Numbered handles make overlapping or bypassed bands easy to distinguish.
        // Draw active band numbers only at useful visual sizes.
        if (getWidth() >= 540 && band.enabled)
        {
            g.setColour(juce::Colour(0xffe5ffe7));
            g.setFont(juce::Font(juce::FontOptions(10.f).withStyle("Bold")));
            g.drawText(juce::String(b + 1), juce::Rectangle<int>((int)x - 8, (int)y - 9, 16, 18),
                       juce::Justification::centred, false);
        }
    }
    if (!anyEnabled)
    {
        g.setColour(juce::Colour(0xffbce8b4).withAlpha(.9f));
        g.setFont(juce::Font(juce::FontOptions(15.0f)));
        g.drawFittedText("ALL BANDS BYPASSED  -  select a node and enable ACTIVE, or click RESET CURVE",
                         juce::Rectangle<int>(18, 18, juce::jmax(0, getWidth() - 390), 28),
                         juce::Justification::centredLeft, 1);
    }
}

void SkellyEQRestoredEditor::ResponseGraph::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return; // Avoid async host popup paths during crash isolation.
    dragged = findBand(e.position);
    if (dragged < 0) return;
    origin = e.position;
    const auto band = processor.bands()[(size_t)dragged];
    originalFrequency = (float)band.freq;
    originalGain = (float)band.gain;
    originalQ = (float)band.q;
    if (onSelection) onSelection(dragged);
    else selected = dragged;
    // Disabled points can be selected but must not be moved accidentally.
    if (!band.enabled) { dragged = -1; repaint(); return; }
    beginDragParam(param(dragged, e.mods.isAltDown() ? "q" : "freq"));
    if (!e.mods.isAltDown() && SkellyDSP::filterUsesGain(band.type))
    {
        gainGestureParam = param(dragged, "gain");
        if (gainGestureParam != nullptr) gainGestureParam->beginChangeGesture();
    }
    repaint();
}
void SkellyEQRestoredEditor::ResponseGraph::mouseDrag(const juce::MouseEvent& e)
{
    if (dragged < 0) return;
    const auto delta = e.position - origin;
    const float fine = e.mods.isShiftDown() ? .15f : 1.f;
    if (e.mods.isAltDown())
    {
        // Switching to Q closes the gain gesture once, not on every mouse event.
        if (gainGestureParam != nullptr)
        {
            gainGestureParam->endChangeGesture();
            gainGestureParam = nullptr;
        }
        beginDragParam(param(dragged, "q"));
        setParam(dragged, "q", juce::jlimit(.1f, 18.f, originalQ * std::exp(-delta.y * fine * .012f)));
    }
    else
    {
        beginDragParam(param(dragged, "freq"));
        const bool hasGain = SkellyDSP::filterUsesGain(processor.bands()[(size_t)dragged].type);
        if (hasGain && gainGestureParam == nullptr)
        {
            gainGestureParam = param(dragged, "gain");
            if (gainGestureParam != nullptr) gainGestureParam->beginChangeGesture();
        }
        if (!e.mods.isCtrlDown() || std::abs(delta.x) >= std::abs(delta.y) || !hasGain)
            setParam(dragged, "freq", frequencyForX(xForFrequency(originalFrequency) + delta.x * fine));
        if (hasGain && (!e.mods.isCtrlDown() || std::abs(delta.y) > std::abs(delta.x)))
            setParam(dragged, "gain", gainForY(yForGain(originalGain) + delta.y * fine));
    }
}
void SkellyEQRestoredEditor::ResponseGraph::mouseUp(const juce::MouseEvent&)
{
    endDragParam();
    dragged = -1;
}
void SkellyEQRestoredEditor::ResponseGraph::mouseDoubleClick(const juce::MouseEvent& e)
{
    int band = findBand(e.position);
    if (band >= 0)
    {
        setParam(band, "enabled", processor.bands()[(size_t)band].enabled ? 0.f : 1.f);
        return;
    }
    const auto bands = processor.bands();
    for (int i = 0; i < 8; ++i)
        if (!bands[(size_t)i].enabled)
        {
            // An empty-graph double-click creates a *Peak* node, not a
            // leftover cut/notch shape inherited from the disabled slot.
            // All changes notify the host with discrete automation gestures.
            setParam(i, "type", 0.f);
            for (auto field : {"freq", "gain", "enabled"})
                if (auto* p = param(i, field))
                {
                    const float value = juce::String(field) == "freq" ? frequencyForX(e.position.x)
                                       : juce::String(field) == "gain" ? gainForY(e.position.y) : 1.f;
                    setParam(i, field, value);
                }
            if (onSelection) onSelection(i);
            else selected = i;
            return;
        }
}
void SkellyEQRestoredEditor::ResponseGraph::mouseWheelMove(const juce::MouseEvent& e,
    const juce::MouseWheelDetails& wheel)
{
    int band = findBand(e.position);
    if (band < 0) return;
    if (param(band, "q") != nullptr)
    {
        const float current = (float)processor.bands()[(size_t)band].q;
        setParam(band, "q", juce::jlimit(.1f, 18.f, current * std::exp(wheel.deltaY * .12f)));
    }
}
void SkellyEQRestoredEditor::ResponseGraph::mouseMove(const juce::MouseEvent& e)
{
    int band = findBand(e.position);
    setMouseCursor(band >= 0 ? juce::MouseCursor::PointingHandCursor
                             : juce::MouseCursor::CrosshairCursor);
    if (band < 0) { setTooltip({}); return; }
    const auto cfg = processor.bands()[(size_t)band];
    const juce::String frequency = cfg.freq >= 1000.0
        ? juce::String(cfg.freq / 1000.0, 2) + " kHz"
        : juce::String(cfg.freq, 1) + " Hz";
    const juce::String gainText = SkellyDSP::filterUsesGain(cfg.type)
        ? juce::String(cfg.gain, 1) + " dB" : "Gain N/A";
    setTooltip("Band " + juce::String(band + 1) + "  |  " + frequency
               + "  |  " + gainText + "  |  Q "
               + juce::String(cfg.q, 2)
               + (cfg.enabled ? "" : "  |  BYPASSED"));
}

SkellyEQRestoredEditor::SkellyEQRestoredEditor(SkellyEQAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p), graph(p, selectedBand)
{
    SkellyTrace::checkpoint("restored-editor: constructor ENTER");
    // Genuine blank frame and independently drawn interface; no baked-in controls.
    skin = loadArtwork("frame");
    skull = loadArtwork("skull_emblem");
    if (!skull.isValid()) skull = loadArtwork("skull");
    if (!skin.isValid())
        SkellyTrace::checkpoint("restored-editor: blank frame missing; using vector fallback");
    selectedBand = juce::jlimit(0, 7,
        (int)processor.state.state.getProperty("selectedBand", 0));
    setLookAndFeel(&look);
    // All bands are loaded from the saved host state. Never silently replace a
    // preset with the screenshot's demo curve when an editor is opened.
    addAndMakeVisible(graph);
    graph.onSelection = [this](int b) { selectBand(b); };

    configureChoice(bandSelect, {"BAND 1", "BAND 2", "BAND 3", "BAND 4",
                                  "BAND 5", "BAND 6", "BAND 7", "BAND 8"});
    configureChoice(typeSelect, {"Peak", "L-Shelf", "H-Shelf", "L-Cut",
                                  "H-Cut", "Notch", "Band", "Tilt"});
    configureChoice(phaseSelect, {"LINEAR FIR", "MIN PHASE", "ZERO LAT."});
    configureChoice(slopeSelect, {"12 dB", "24 dB", "36 dB", "48 dB"});
    configureChoice(modeSelect, {"L/R", "M/S"});
    configureChoice(analyzeSelect, {"OFF", "PRE", "POST", "ON"});
    configureChoice(channelSelect, {"STEREO", "LEFT", "RIGHT", "MID", "SIDE"});
    for (auto* c : {&bandSelect, &typeSelect, &phaseSelect, &slopeSelect, &modeSelect, &analyzeSelect, &channelSelect})
        addAndMakeVisible(*c);

    for (auto* s : {&frequency, &gain, &q, &output, &analyzeAmount})
    {
        configureDial(*s);
        addAndMakeVisible(*s);
    }
    // Formatting is applied again after attachments are bound below.
    // Double click returns to the correct default for the currently selected
    // band; a hard-coded 0 would otherwise reset every frequency to 20 Hz.
    frequency.setDoubleClickReturnValue(false, 0.0);
    q.setDoubleClickReturnValue(true, 1.0);
    gain.setDoubleClickReturnValue(true, 0.0);
    output.setDoubleClickReturnValue(true, 0.0);
    analyzeAmount.setDoubleClickReturnValue(true, 100.0);
    analyzeAmount.setSliderStyle(juce::Slider::LinearHorizontal);
    analyzeAmount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 19);
    enabled.setButtonText("ON");
    solo.setButtonText("SOLO");
    addAndMakeVisible(enabled);
    addAndMakeVisible(solo);
    solo.setColour(juce::ToggleButton::textColourId, bright);
    for (auto* b : {&options, &about, &previous, &next, &resetCurve,
                    &saveA, &loadA, &saveB, &loadB})
        addAndMakeVisible(*b);
    enabled.setColour(juce::ToggleButton::textColourId, bright);
    about.setTooltip("Skelly EQ 2.7 | Clean phase switching + linear FIR");
    about.setButtonText("v2.7");
    about.setEnabled(false); // Don't open host-modal dialogs on plugin close.
    options.setTooltip("Cycle graph dB display range");
    options.onClick = [this]
    {
        const auto nextRange = ((int)processor.state.getRawParameterValue("graphRange")->load() + 1) % 4;
        setGlobal("graphRange", (float)nextRange);
    };
    previous.onClick = [this] { selectBand((selectedBand + 7) % 8); };
    next.onClick = [this] { selectBand((selectedBand + 1) % 8); };
    resetCurve.setButtonText("RESET CURVE");
    resetCurve.setTooltip("Apply the eight-band starting curve to all bands. Replaces their current settings; undo is host-dependent.");
    resetCurve.onClick = [this] { restoreReferenceCurve(); };
    saveA.onClick = [this] { processor.storeSnapshot(0); snapshotStatus = "A STORED"; snapshotTicks = 75; repaint(); };
    loadA.onClick = [this]
    {
        const bool available = processor.state.state.getChildWithName(juce::Identifier("snapshotA")).isValid();
        if (available) processor.loadSnapshot(0);
        snapshotStatus = available ? "A RECALL" : "A EMPTY";
        snapshotTicks = 75;
        graph.repaint(); repaint();
    };
    saveB.onClick = [this] { processor.storeSnapshot(1); snapshotStatus = "B STORED"; snapshotTicks = 75; repaint(); };
    loadB.onClick = [this]
    {
        const bool available = processor.state.state.getChildWithName(juce::Identifier("snapshotB")).isValid();
        if (available) processor.loadSnapshot(1);
        snapshotStatus = available ? "B RECALL" : "B EMPTY";
        snapshotTicks = 75;
        graph.repaint(); repaint();
    };
    for (auto* b : {&saveA, &loadA, &saveB, &loadB})
        b->setTooltip("A/B snapshots are stored inside the plugin state");

    const char* typeNames[] = {"LC", "BELL", "HC", "NOTCH", "BAND"};
    const char* typeHints[] = {"Low cut", "Bell/peak", "High cut", "Notch", "Band pass"};
    const int typeIds[] = {3, 0, 4, 5, 6};
    for (int i = 0; i < 5; ++i)
    {
        typeButtons[i].setButtonText(typeNames[i]);
        typeButtons[i].setTooltip(typeHints[i]);
        typeButtons[i].onClick = [this, type = typeIds[i]] { setBandType(type); };
        addAndMakeVisible(typeButtons[i]);
    }
    outputAttachment = std::make_unique<SliderAttachment>(processor.state, "outputGain", output);
    analyzeAttachment = std::make_unique<ComboAttachment>(processor.state, "analyzeMode", analyzeSelect);
    analyzeAmountAttachment = std::make_unique<SliderAttachment>(processor.state, "analyzeAmount", analyzeAmount);
    phaseAttachment = std::make_unique<ComboAttachment>(processor.state, "phaseMode", phaseSelect);
    slopeAttachment = std::make_unique<ComboAttachment>(processor.state, "slope", slopeSelect);
    modeAttachment = std::make_unique<ComboAttachment>(processor.state, "stereoMode", modeSelect);
    bandSelect.onChange = [this] { selectBand(bandSelect.getSelectedId() - 1); };
    bandSelect.setSelectedId(selectedBand + 1, juce::dontSendNotification);
    rebindBand();
    formatDecibels(output);
    formatPercent(analyzeAmount);

    buildTag.setText("V2.7 | LINEAR FIR",
                     juce::dontSendNotification);
    buildTag.setFont(juce::Font(juce::FontOptions(11.0f)));
    buildTag.setColour(juce::Label::textColourId, bright);
    buildTag.setJustificationType(juce::Justification::centred);
    // Version appears in the header controls, never as duplicated bitmap text.
    buildTag.setVisible(false);

    // Direct band access without hiding selection behind a drop-down.
    for (int b = 0; b < 8; ++b)
    {
        auto& tab = bandTabs[(size_t)b];
        tab.setName("SkellyBandTab");
        tab.setButtonText(juce::String(b + 1));
        tab.setTooltip("Select EQ band " + juce::String(b + 1));
        tab.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        tab.onClick = [this, b] { selectBand(b); };
        addAndMakeVisible(tab);
    }
    for (int b = 0; b < 8; ++b)
        bandTabs[(size_t)b].setToggleState(b == selectedBand, juce::dontSendNotification);
    for (auto* btn : {&previous, &next, &options, &saveA, &loadA, &saveB, &loadB,
                      &resetCurve, &typeButtons[0], &typeButtons[1], &typeButtons[2],
                      &typeButtons[3], &typeButtons[4]})
        btn->setMouseCursor(juce::MouseCursor::PointingHandCursor);
    for (auto* combo : {&bandSelect, &typeSelect, &phaseSelect, &slopeSelect,
                        &modeSelect, &analyzeSelect, &channelSelect})
        combo->setMouseCursor(juce::MouseCursor::PointingHandCursor);
    frequency.setTooltip("Selected band frequency. Drag or enter Hz/kHz. Double-click resets to band default.");
    gain.setTooltip("Selected band gain in dB. Shift-drag for fine adjustment.");
    q.setTooltip("Selected band filter Q / width.");
    output.setTooltip("Global output gain in dB.");
    analyzeAmount.setTooltip("FFT display intensity (does not change audio).");
    phaseSelect.setTooltip("True symmetric FIR in Linear mode (2049 taps / 1152 samples total latency). Min and Zero Latency use zero-delay biquads. Switching phase updates host delay compensation.");
    resetCurve.setTooltip("Replace all eight band settings with the reference contour. Existing preset data is overwritten.");

    // Avoid showing stale spectrum from a previous editor session.
    processor.analyzer.discardPending();
    // Precompute once: no trigonometry or window allocation in the timer.
    for (int j = 0; j < 4096; ++j)
        hannWindow[(size_t)j] = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * (float)j / 4095.f);
    // V1.4 processor and safe editor are preserved as a rollback path.
    // Default = 40% smaller than the original 1784x882 screenshot canvas.
    // Do not restore the old 1338x661 default or JUCE will still open oversized.
    setResizable(true, true);
    setResizeLimits(856, 423, 1605, 794);
    setSize(1070, 529);
    if (auto* constraint = getConstrainer())
        constraint->setFixedAspectRatio((double)baseW / (double)baseH);
    startTimerHz(30); // FFT runs on message thread, not DSP thread.
    SkellyTrace::checkpoint("restored-editor: constructor EXIT");
}

SkellyEQRestoredEditor::~SkellyEQRestoredEditor()
{
    SkellyTrace::checkpoint("restored-editor: destructor ENTER");
    stopTimer();
    graph.onSelection = nullptr;
    bandSelect.onChange = nullptr;
    options.onClick = nullptr;
    previous.onClick = nullptr;
    next.onClick = nullptr;
    resetCurve.onClick = nullptr;
    saveA.onClick = nullptr; loadA.onClick = nullptr;
    saveB.onClick = nullptr; loadB.onClick = nullptr;
    for (auto& b : typeButtons) b.onClick = nullptr;
    for (auto& b : bandTabs) b.onClick = nullptr;
    freqAttachment.reset(); gainAttachment.reset(); qAttachment.reset();
    outputAttachment.reset(); typeAttachment.reset(); phaseAttachment.reset();
    slopeAttachment.reset(); modeAttachment.reset(); enabledAttachment.reset();
    analyzeAttachment.reset(); analyzeAmountAttachment.reset();
    channelAttachment.reset(); soloAttachment.reset();
    setLookAndFeel(nullptr);
    SkellyTrace::checkpoint("restored-editor: destructor EXIT");
}

void SkellyEQRestoredEditor::selectBand(int band)
{
    if (!juce::isPositiveAndBelow(band, 8)) return;
    if (band != selectedBand)
    {
        selectedBand = band;
        processor.state.state.setProperty("selectedBand", selectedBand, nullptr);
        rebindBand();
    }
    bandSelect.setSelectedId(selectedBand + 1, juce::dontSendNotification);
    for (int b = 0; b < 8; ++b)
        bandTabs[(size_t)b].setToggleState(b == selectedBand, juce::dontSendNotification);
    graph.repaint();
    repaint(juce::Rectangle<float>(52.f * getWidth() / baseW, 86.f * getHeight() / baseH,
                                  526.f * getWidth() / baseW, 27.f * getHeight() / baseH).toNearestInt());
}
void SkellyEQRestoredEditor::rebindBand()
{
    freqAttachment.reset(); gainAttachment.reset(); qAttachment.reset();
    typeAttachment.reset(); enabledAttachment.reset();
    soloAttachment.reset(); channelAttachment.reset();
    freqAttachment = std::make_unique<SliderAttachment>(processor.state, bandId(selectedBand, "freq"), frequency);
    gainAttachment = std::make_unique<SliderAttachment>(processor.state, bandId(selectedBand, "gain"), gain);
    qAttachment = std::make_unique<SliderAttachment>(processor.state, bandId(selectedBand, "q"), q);
    // configureDial's zero reset is correct for gain, but invalid for Q;
    // the actual neutral Q should be restored when rebinding a band.
    if (auto* p = processor.state.getParameter(bandId(selectedBand, "q")))
        q.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
    if (auto* p = processor.state.getParameter(bandId(selectedBand, "freq")))
        frequency.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
    typeAttachment = std::make_unique<ComboAttachment>(processor.state, bandId(selectedBand, "type"), typeSelect);
    enabledAttachment = std::make_unique<ButtonAttachment>(processor.state, bandId(selectedBand, "enabled"), enabled);
    soloAttachment = std::make_unique<ButtonAttachment>(processor.state, bandId(selectedBand, "solo"), solo);
    channelAttachment = std::make_unique<ComboAttachment>(processor.state, bandId(selectedBand, "channel"), channelSelect);
    formatFrequency(frequency);
    formatDecibels(gain);
    formatQ(q);
    updateBandControlStates();
}
void SkellyEQRestoredEditor::updateBandControlStates()
{
    const auto bands = processor.bands();
    const auto& current = bands[(size_t)selectedBand];
    const bool usesGain = SkellyDSP::filterUsesGain(current.type);
    gain.setEnabled(usesGain);
    gain.setTooltip(usesGain ? "Selected band gain in dB. Shift-drag for fine adjustment."
                             : "Gain is not used by this filter. Switch to Peak/Shelf/Tilt to adjust gain.");
    for (int i = 0; i < 8; ++i)
    {
        auto& tab = bandTabs[(size_t)i];
        const auto& b = bands[(size_t)i];
        const auto text = "Band " + juce::String(i + 1) +
            (b.enabled ? (b.solo ? " - SOLO" : " - Active") : " - Bypassed");
        if (tab.getTooltip() != text) tab.setTooltip(text);
        const auto colour = !b.enabled ? juce::Colour(0xff172319)
                          : b.solo ? juce::Colour(0xff4d3616)
                          : juce::Colour(0xff0a2410);
        if (tab.findColour(juce::TextButton::buttonColourId) != colour)
            tab.setColour(juce::TextButton::buttonColourId, colour);
    }
}
void SkellyEQRestoredEditor::setGlobal(const juce::String& key, float value)
{
    if (auto* p = processor.state.getParameter(key))
    {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(value)); p->endChangeGesture();
    }
    graph.repaint();
    repaint(); // Axis labels must update when OPTION cycles the graph range.
}
void SkellyEQRestoredEditor::setBandType(int type)
{
    setGlobal(bandId(selectedBand, "type"), (float)type);
}

void SkellyEQRestoredEditor::restoreReferenceCurve()
{
    // This is deliberately user-triggered: saved FL Studio presets are NEVER
    // overwritten just because the editor opened.
    static constexpr float frequencies[] = {25.f, 65.f, 130.f, 300.f, 900.f, 7000.f, 10000.f, 19000.f};
    static constexpr float gains[] = {1.f, -2.f, -4.f, -3.f, -11.f, -2.f, 7.f, -1.5f};
    for (int band = 0; band < 8; ++band)
    {
        setGlobal(bandId(band, "type"), 0.f);
        setGlobal(bandId(band, "freq"), frequencies[band]);
        setGlobal(bandId(band, "gain"), gains[band]);
        setGlobal(bandId(band, "q"), 1.0f);
        setGlobal(bandId(band, "channel"), 0.f);
        setGlobal(bandId(band, "solo"), 0.f);
        setGlobal(bandId(band, "enabled"), 1.f);
    }
    selectBand(4);
    graph.repaint();
}

void SkellyEQRestoredEditor::drawMeters(juce::Graphics& g)
{
    // Exactly matches the controls in resized(): no overpaint or screenshot labels.
    panel(g, {909.f, 83.f, 145.f, 353.f}, 7.f);
    label(g, "OUTPUT  /  PEAK + RMS", 915.f, 88.f, 134.f, 16.f, 10.3f, bright);
    separator(g, 918.f, 1045.f, 107.f);
    constexpr float top = 116.f, height = 100.f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float x0 = 931.f + (float)ch * 58.f;
        for (int kind = 0; kind < 2; ++kind)
        {
            const float x = x0 + kind * 14.f;
            const float reading = kind == 0 ? meterPeak[ch] : meterRms[ch];
            const float db = juce::jlimit(-60.f, 6.f,
                juce::Decibels::gainToDecibels(juce::jmax(0.f, reading), -60.f));
            const float level = juce::jlimit(0.f, 1.f, (db + 60.f) / 66.f);
            g.setColour(juce::Colour(0xff102016));
            g.fillRoundedRectangle(x, top, 10.f, height, 2.f);
            for (int segment = 0; segment < 20; ++segment)
            {
                const float y = top + height - (segment + 1) * 5.f;
                const bool lit = (float)(segment + 1) / 20.f <= level;
                const juce::Colour c = segment > 17 ? juce::Colour(0xffffbd66)
                                     : segment > 15 ? juce::Colour(0xffaaff5e)
                                     : juce::Colour(0xff47fa6b);
                g.setColour(lit ? c : juce::Colour(0xff183421));
                g.fillRoundedRectangle(x + 1.f, y + 1.f, 8.f, 3.5f, .8f);
            }
            g.setColour(juce::Colour(0xff346b3f));
            g.drawRoundedRectangle(x, top, 10.f, height, 2.f, .8f);
        }
        g.setColour(clipHoldTicks[ch] > 0 ? juce::Colour(0xffffac4a)
                                            : juce::Colour(0xff285332));
        g.fillEllipse(x0 + 5.f, 109.f, 7.f, 6.f);
        label(g, ch == 0 ? "L" : "R", x0, 219.f, 26.f, 12.f, 10.f, bright);
        const float dbPeak = juce::Decibels::gainToDecibels(meterPeak[ch], -60.f);
        label(g, juce::String(dbPeak, 1), x0 - 9.f, 231.f, 46.f, 13.f, 10.f, bright);
    }
    g.setColour(juce::Colour(0xff91a99b));
    g.setFont(juce::Font(juce::FontOptions(8.7f)));
    for (const int db : {6, 0, -18, -36, -54})
    {
        const float y = top + height - (db + 60.f) / 66.f * height;
        g.drawText(db > 0 ? juce::String("+6") : juce::String(db),
                   1030, (int)y - 6, 21, 12, juce::Justification::centredLeft, false);
    }
    separator(g, 917.f, 1045.f, 247.f);
    label(g, "FILTER SHAPE", 917.f, 249.f, 127.f, 13.f, 10.3f, bright);
    label(g, "TYPE", 917.f, 318.f, 43.f, 20.f, 9.7f, green);
    label(g, "ROUTE", 917.f, 342.f, 43.f, 20.f, 9.7f, green);
    label(g, "PHASE", 917.f, 366.f, 43.f, 20.f, 9.7f, green);
    label(g, "SLOPE", 917.f, 390.f, 43.f, 20.f, 9.7f, green);
}

void SkellyEQRestoredEditor::paint(juce::Graphics& g)
{
    g.fillAll(dark);
    const float sx = (float)getWidth() / baseW;
    const float sy = (float)getHeight() / baseH;
    juce::Graphics::ScopedSaveState guard(g);
    g.addTransform(juce::AffineTransform::scale(sx, sy));

    // The clean graphic is FRAME ONLY, not an image of controls.
    if (skin.isValid())
        g.drawImage(skin, 0.f, 0.f, baseW, baseH, 0, 0, skin.getWidth(), skin.getHeight());
    else
    {
        g.setColour(juce::Colour(0xff08190d));
        g.fillRect(0.f, 0.f, baseW, baseH);
        g.setColour(green.withAlpha(.5f));
        g.drawRoundedRectangle(juce::Rectangle<float>(6.f, 6.f, baseW-12.f, baseH-12.f), 8.f, 1.5f);
    }

    // Compact header: real text and functional buttons, never baked fake UI.
    panel(g, {19.f, 11.f, 323.f, 58.f}, 7.f);
    panel(g, {615.f, 14.f, 439.f, 53.f}, 7.f);
    if (skull.isValid())
    {
        g.setColour(green.withAlpha(.10f));
        g.fillEllipse(473.f, 0.f, 128.f, 77.f);
        g.setOpacity(.98f);
        g.drawImage(skull, 478.f, 0.f, 116.f, 77.f, 0, 0, skull.getWidth(), skull.getHeight());
        g.setOpacity(1.f);
    }
    g.setColour(juce::Colour(0xffb0ffb5));
    g.setFont(juce::Font(juce::FontOptions(29.f).withStyle("Bold")));
    g.drawFittedText("SKELLY EQ", juce::Rectangle<int>(34, 16, 288, 34),
                     juce::Justification::centredLeft, 1);
    g.setColour(juce::Colour(0xffbedac1));
    g.setFont(juce::Font(juce::FontOptions(10.5f).withStyle("Bold")));
    g.drawText("BONE-DEEP PARAMETRIC", 37, 50, 277, 15, juce::Justification::centredLeft);
    label(g, (int)processor.state.getRawParameterValue("phaseMode")->load()==0
        ? "8 BANDS  /  2049-TAP LINEAR FIR" : "8 BANDS  /  4096 FFT  /  MIN PHASE DSP",
        630.f, 69.f, 242.f, 12.f,
          9.f, juce::Colour(0xff79a784));
    if (snapshotTicks > 0)
        label(g, snapshotStatus, 892.f, 69.f, 151.f, 12.f, 9.2f, juce::Colour(0xffffc976));

    // Plot's toolbar is ABOVE graph component. No modal/overlay obstructs nodes.
    panel(g, {20.f, 83.f, 884.f, 353.f}, 7.f);
    const auto bs = processor.bands();
    const auto& band = bs[(size_t)juce::jlimit(0, 7, selectedBand)];
    const juce::String freqText = band.freq >= 1000.0
        ? juce::String(band.freq / 1000.0, 2) + " kHz"
        : juce::String(band.freq, band.freq < 100.0 ? 1 : 0) + " Hz";
    const juce::String gainText = juce::String(band.gain > 0.05 ? "+" : "")
        + juce::String(band.gain, 1) + " dB";
    g.setColour(juce::Colour(0xff142c1a));
    g.fillRoundedRectangle(50.f, 88.f, 439.f, 23.f, 4.f);
    g.setColour(juce::Colour(0xff397e47));
    g.drawRoundedRectangle(50.f, 88.f, 439.f, 23.f, 4.f, .9f);
    label(g, "B" + juce::String(selectedBand + 1), 54.f, 91.f, 43.f, 17.f, 11.f,
          juce::Colour(0xffffc96b));
    label(g, freqText, 96.f, 91.f, 108.f, 17.f, 11.f, bright);
    label(g, SkellyDSP::filterUsesGain(band.type) ? gainText : "GAIN N/A",
          209.f, 91.f, 99.f, 17.f, 11.f,
          SkellyDSP::filterUsesGain(band.type) ? bright : juce::Colour(0xff819b85));
    label(g, "Q " + juce::String(band.q, 2), 315.f, 91.f, 73.f, 17.f, 11.f, bright);
    label(g, band.enabled ? (band.solo ? "SOLO" : "ACTIVE") : "BYPASS", 393.f, 91.f, 91.f, 17.f, 10.f,
          band.solo ? juce::Colour(0xffffc976)
                    : band.enabled ? bright : juce::Colour(0xffa7aaa7));
    static constexpr const char* bandTypes[] = {"BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT",
                                                 "HIGH CUT", "NOTCH", "BAND PASS", "TILT"};
    label(g, bandTypes[juce::jlimit(0, 7, band.type)], 491.f, 91.f, 88.f, 17.f, 9.5f, bright);

    const int rangeValues[] = {6,12,18,30};
    const int range = rangeValues[juce::jlimit(0, 3,
        (int)processor.state.getRawParameterValue("graphRange")->load())];
    const int step = range == 6 ? 2 : range <= 18 ? 6 : 10;
    for (int db = -range; db <= range; db += step)
    {
        const float yy = 120.f + ((float)range - (float)db) / (2.f * (float)range) * 274.f;
        label(g, (db > 0 ? "+" : "") + juce::String(db),
              21.f, yy - 8.f, 28.f, 16.f, 10.5f,
              db == 0 ? bright : green.withAlpha(.80f));
    }
    const int ticks[] = {20,50,100,200,500,1000,2000,5000,10000,20000};
    for (const int f : ticks)
    {
        const float px = 50.f + 14.f + std::log((float)f / 20.f) / std::log(1000.f) * (836.f - 28.f);
        label(g, f >= 1000 ? juce::String(f / 1000) + "K" : juce::String(f),
              px-19.f, 396.f, 38.f, 15.f, 10.5f, bright);
    }
    label(g, "DRAG NODES  •  WHEEL Q  •  SHIFT FINE", 58.f, 417.f, 371.f, 14.f,
          9.1f, green.withAlpha(.88f));
    label(g, "MODE", 458.f, 417.f, 47.f, 14.f, 9.5f, green);
    label(g, "FFT", 578.f, 417.f, 30.f, 14.f, 9.5f, green);
    label(g, "LEVEL", 692.f, 417.f, 44.f, 14.f, 9.5f, green);
    drawMeters(g);

    // Bottom control deck; readable native-size labels and separate knob readouts.
    panel(g, {20.f, 445.f, 1034.f, 73.f}, 7.f);
    g.setColour(green.withAlpha(.20f));
    g.drawVerticalLine(375, 452.f, 510.f);
    g.drawVerticalLine(743, 452.f, 510.f);
    label(g, "SELECT BAND", 32.f, 449.f, 195.f, 14.f, 10.f, bright);
    label(g, "FREQUENCY", 391.f, 447.f, 100.f, 13.f, 10.3f, bright);
    const auto selectedType = processor.bands()[(size_t)selectedBand].type;
    label(g, SkellyDSP::filterUsesGain(selectedType) ? "GAIN" : "GAIN / N.A.",
          505.f, 447.f, 100.f, 13.f, 10.3f,
          SkellyDSP::filterUsesGain(selectedType) ? bright : juce::Colour(0xff69816f));
    label(g, "Q / WIDTH", 619.f, 447.f, 100.f, 13.f, 10.3f, bright);
    label(g, "OUTPUT", 767.f, 447.f, 100.f, 13.f, 10.3f, bright);
    label(g, (int)processor.state.getRawParameterValue("phaseMode")->load()==0
        ? "LINEAR FIR +1152 SAMPLES" : "ZERO-LATENCY IIR ENGINE",
        873.f, 462.f, 171.f, 15.f, 10.f, bright);
}

void SkellyEQRestoredEditor::resized()
{
    const float sx = (float)getWidth() / baseW, sy = (float)getHeight() / baseH;
    auto put = [sx, sy](juce::Component& c, float x, float y, float w, float h)
    {
        c.setBounds(juce::Rectangle<float>(x * sx, y * sy, w * sx, h * sy).toNearestInt());
    };
    put(graph, 50, 120, 836, 274);
    // Header actions: each independently clickable, with no overlap.
    put(about,   623, 30, 51, 26);
    put(options, 680, 30, 62, 26);
    put(saveA,   748, 30, 66, 26);
    put(loadA,   819, 30, 66, 26);
    put(saveB,   890, 30, 66, 26);
    put(loadB,   961, 30, 84, 26);
    // Selected-band toolbar.
    put(enabled,   586, 88, 76, 24);
    put(solo,      667, 88, 64, 24);
    put(resetCurve,737, 88, 150, 24);

    // Right rail, custom meters above 5 filter shapes and global choices.
    put(typeButtons[0], 918, 264, 41, 23);
    put(typeButtons[1], 963, 264, 41, 23);
    put(typeButtons[2],1008, 264, 41, 23);
    put(typeButtons[3], 918, 290, 64, 22);
    put(typeButtons[4], 987, 290, 62, 22);
    put(typeSelect,    966, 316, 83, 22);
    put(channelSelect, 966, 340, 83, 22);
    put(phaseSelect,   966, 364, 83, 22);
    put(slopeSelect,   966, 388, 83, 22);
    put(modeSelect,    506, 414, 70, 19);
    put(analyzeSelect, 608, 414, 82, 19);
    put(analyzeAmount, 738, 414, 148, 19);

    // Bottom controls: 8 direct band selectors plus compact, aligned dials.
    put(bandSelect, 35, 465, 128, 22);
    for (int b = 0; b < 8; ++b)
        put(bandTabs[(size_t)b], 35.f + 31.f * b, 490.f, 28.f, 22.f);
    put(previous, 290, 465, 31, 22);
    put(next, 326, 465, 31, 22);
    put(frequency, 394, 459, 93, 55);
    put(gain,      508, 459, 93, 55);
    put(q,         622, 459, 93, 55);
    put(output,    768, 459, 93, 55);
    // Analyzer controls in the graph's slim status bar; bottom right displays a hint.
}

void SkellyEQRestoredEditor::timerCallback()
{
    // SPSC: audio thread writes, editor reads. Hann 4096, 75% overlap,
    // cached window. FFT and 1/3-octave smoothing stay on message thread.
    const auto mode = (int) processor.state.getRawParameterValue("analyzeMode")->load();
    graph.spectrumOpacity = juce::jlimit(0.f, 1.f,
        processor.state.getRawParameterValue("analyzeAmount")->load() * .01f);
    if (mode != 0)
    {
        std::array<float, 2048> popped{};
        int frames = 0;
        for (int tries = 0; tries < 8; ++tries)
        {
            const int n = processor.analyzer.pop(popped.data(), (int)popped.size());
            if (n <= 0) break;
            for (int i = 0; i < n; ++i)
            {
                fftInput[(size_t)fftFill++] = popped[(size_t)i];
                if (fftFill == 4096)
                {
                    // Process at most 5 frames per timer tick to prevent a
                    // busy session from monopolizing FL Studio's GUI thread.
                    if (frames++ < 5)
                    {
                        std::fill(fftWork.begin(), fftWork.end(), 0.f);
                        for (int j = 0; j < 4096; ++j)
                            fftWork[(size_t)j] = fftInput[(size_t)j] * hannWindow[(size_t)j];
                        fft.performFrequencyOnlyForwardTransform(fftWork.data());
                        const double sr = processor.sampleRateHz();
                        for (int k = 0; k < 256; ++k)
                        {
                            const float x = (float)k / 255.f;
                            const float freq = 20.f * std::pow(1000.f, x);
                            if (freq >= sr * .5f)
                            {
                                spectrumScratch[(size_t)k] = -90.f;
                                continue;
                            }
                            const int bin = juce::jlimit(1, 2047,
                                (int)std::round(freq * 4096.f / (float)sr));
                            // Hann's coherent gain ~2048; this normalizes the
                            // two-sided magnitude to about 0 dBFS for a 0 dBFS sine.
                            const float amplitude = fftWork[(size_t)bin] / 1024.f;
                            spectrumScratch[(size_t)k] = juce::jlimit(-90.f, 6.f,
                                juce::Decibels::gainToDecibels(amplitude, -90.f));
                        }
                        // 5-point logarithmic-bin smoothing; smoother than raw
                        // bin picking, without allocation or FFT on audio thread.
                        for (int k = 0; k < 256; ++k)
                        {
                            const auto sample = [this](int idx)
                            {
                                return spectrumScratch[(size_t)juce::jlimit(0,255,idx)];
                            };
                            const float db = (sample(k-2) + 2.f*sample(k-1) +
                                              3.f*sample(k) + 2.f*sample(k+1) +
                                              sample(k+2)) / 9.f;
                            auto& old = graph.spectrumDb[(size_t)k];
                            old += (db - old) * (db > old ? .50f : .18f);
                        }
                        graph.hasSpectrum = true;
                    }
                    // Move 75% of samples, making the hop 1024 (25% FFT size).
                    for (int j = 0; j < 3072; ++j)
                        fftInput[(size_t)j] = fftInput[(size_t)(j + 1024)];
                    fftFill = 3072;
                }
            }
            if (n < (int)popped.size()) break;
        }
    }
    else
    {
        processor.analyzer.discardPending(); // Editor is the sole reader.
        graph.hasSpectrum = false;
        fftFill = 0;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        const float currentPeak = juce::jmax(0.f, processor.peaks[ch].load(std::memory_order_relaxed));
        const float currentRms = juce::jmax(0.f, processor.rms[ch].load(std::memory_order_relaxed));
        meterPeak[ch] = juce::jmax(currentPeak, meterPeak[ch] * .78f);
        meterRms[ch] = juce::jmax(currentRms, meterRms[ch] * .78f);
        clipHoldTicks[ch] = currentPeak >= 1.f ? 45 : juce::jmax(0, clipHoldTicks[ch] - 1);
    }
    if (snapshotTicks > 0)
    {
        --snapshotTicks;
        repaint(juce::Rectangle<float>(889.f * getWidth() / baseW, 68.f * getHeight() / baseH,
                                       159.f * getWidth() / baseW, 15.f * getHeight() / baseH).toNearestInt());
    }
    updateBandControlStates();
    const int selectedType = (int)processor.state.getRawParameterValue(bandId(selectedBand, "type"))->load();
    const int types[] = {3, 0, 4, 5, 6};
    for (int i = 0; i < 5; ++i)
        typeButtons[i].setToggleState(selectedType == types[i], juce::dontSendNotification);
    // Keep direct-select tabs in sync even when automation restores a different selection.
    for (int b = 0; b < 8; ++b)
        bandTabs[(size_t)b].setToggleState(b == selectedBand, juce::dontSendNotification);
    graph.repaint();
    const float sx = (float)getWidth() / baseW, sy = (float)getHeight() / baseH;
    repaint(juce::Rectangle<float>(909.f * sx, 83.f * sy, 145.f * sx, 168.f * sy).toNearestInt());
    repaint(juce::Rectangle<float>(50.f * sx, 88.f * sy, 529.f * sx, 24.f * sy).toNearestInt());
}
