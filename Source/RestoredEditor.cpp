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
constexpr float baseW = 1784.0f;
constexpr float baseH = 882.0f;
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
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 114, 24);
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
    const auto base = juce::Colour(active ? 0xff1a5b24 : 0xff09180d);
    g.setGradientFill(juce::ColourGradient(base.brighter(hover ? .23f : .10f), 0.f, 0.f,
                         base.darker(down ? .24f : .05f), 0.f, bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, 6.f);
    const auto stroke = active ? juce::Colour(0xff75ff86)
                               : juce::Colour(hover ? 0xff45d954 : 0xff2b6236);
    g.setColour(stroke.withAlpha(button.isEnabled() ? .90f : .35f));
    g.drawRoundedRectangle(bounds, 6.f, active ? 1.6f : 1.f);
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
    const float size = juce::jlimit(9.f, 14.f, button.getHeight() * .39f);
    g.setFont(juce::Font(juce::FontOptions(size).withStyle("Bold")));
    g.setColour(juce::Colour(button.isEnabled()
        ? (button.getToggleState() ? 0xfff1fff3 : 0xffbceac7)
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
    return juce::Font(juce::FontOptions(juce::jlimit(9.f, 13.5f,
                                                 box.getHeight() * .62f)).withStyle("Bold"));
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
    // Bypassed bands must still be selectable, so a saved FL Studio preset
    // with all eight bands off cannot leave an uneditable empty graph.
    for (int b = 7; b >= 0; --b)
        if (position.getDistanceFrom(
               {xForFrequency((float)bands[(size_t)b].freq),
                juce::jlimit(9.f,(float)getHeight()-9.f,yForGain((float)bands[(size_t)b].gain))}) < 18.0f)
            return b;
    return -1;
}
void SkellyEQRestoredEditor::ResponseGraph::beginDragParam(juce::RangedAudioParameter* p)
{
    endDragParam();
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
            const float x = w * (float)k / (float)(spectrumDb.size() - 1);
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
    bool curveChanged = !curveValid || cachedSlope != slope || cachedRange != range
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
        for (int i = 0; i < 550; ++i)
        {
            const float x = w * (float)i / 549.f;
            const double frequency = frequencyForX(x);
            std::complex<double> combined {1.0, 0.0};
            for (const auto& band : bands)
                combined *= SkellyDSP::bandResponse(band, frequency, processor.sampleRateHz(), slope);
            const float db = (float)juce::Decibels::gainToDecibels(std::abs(combined), -100.0);
            const float y = juce::jlimit(-2.f * h, 3.f * h, yForGain(db));
            if (i == 0) cachedResponse.startNewSubPath(x, y);
            else cachedResponse.lineTo(x, y);
        }
        cachedBands = bands;
        cachedSlope = slope;
        cachedRange = range;
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
    g.setColour(green.withAlpha(.13f));
    g.strokePath(cachedResponse, juce::PathStrokeType(10.f));
    g.setColour(bright.withAlpha(.4f));
    g.strokePath(cachedResponse, juce::PathStrokeType(5.f));
    g.setColour(green);
    g.strokePath(cachedResponse, juce::PathStrokeType(2.6f));
    bool anyEnabled = false;
    for (int pass = 0; pass < 2; ++pass)
    for (int b = 0; b < 8; ++b)
    {
        if ((b == selected) != (pass == 1)) continue;
        const auto& band = bands[(size_t)b];
        anyEnabled |= band.enabled;
        const float x = xForFrequency((float)band.freq);
        // Keep handles reachable when a saved gain exceeds the display range.
        const float y = juce::jlimit(9.f, h-9.f, yForGain((float)band.gain));
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
        if (getWidth() >= 900 && band.enabled)
        {
            g.setColour(juce::Colour(0xffe5ffe7));
            g.setFont(juce::Font(juce::FontOptions(12.f).withStyle("Bold")));
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
    if (!e.mods.isAltDown())
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
        if (gestureParam != param(dragged, "q")) beginDragParam(param(dragged, "q"));
        setParam(dragged, "q", juce::jlimit(.1f, 18.f, originalQ * std::exp(-delta.y * fine * .012f)));
    }
    else
    {
        if (gestureParam != param(dragged, "freq")) beginDragParam(param(dragged, "freq"));
        if (!e.mods.isCtrlDown() || std::abs(delta.x) >= std::abs(delta.y))
            setParam(dragged, "freq", frequencyForX(xForFrequency(originalFrequency) + delta.x * fine));
        if (!e.mods.isCtrlDown() || std::abs(delta.y) > std::abs(delta.x))
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
    if (band < 0) { setTooltip({}); return; }
    const auto cfg = processor.bands()[(size_t)band];
    setTooltip(juce::String(cfg.freq, 1) + " Hz / " + juce::String(cfg.gain, 1)
               + " dB / Q " + juce::String(cfg.q, 2));
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
    configureChoice(phaseSelect, {"LINEAR N/A", "MIN PHASE", "ZERO LAT."});
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
    analyzeAmount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
    enabled.setButtonText("ON");
    solo.setButtonText("SOLO");
    addAndMakeVisible(enabled);
    addAndMakeVisible(solo);
    solo.setColour(juce::ToggleButton::textColourId, bright);
    for (auto* b : {&options, &about, &previous, &next, &resetCurve,
                    &saveA, &loadA, &saveB, &loadB})
        addAndMakeVisible(*b);
    enabled.setColour(juce::ToggleButton::textColourId, bright);
    about.setTooltip("Skelly EQ 2.0 | Refined interface / minimum-phase DSP");
    about.setButtonText("v2.0");
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
    saveA.onClick = [this] { processor.storeSnapshot(0); };
    loadA.onClick = [this] { processor.loadSnapshot(0); };
    saveB.onClick = [this] { processor.storeSnapshot(1); };
    loadB.onClick = [this] { processor.loadSnapshot(1); };
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

    buildTag.setText("V2.0 | SPECTRUM",
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
    phaseSelect.setTooltip("Linear mode is not implemented; audio uses the minimum-phase engine.");
    resetCurve.setTooltip("Replace all eight band settings with the reference contour. Existing preset data is overwritten.");

    // Avoid showing stale spectrum from a previous editor session.
    processor.analyzer.discardPending();
    // Precompute once: no trigonometry or window allocation in the timer.
    for (int j = 0; j < 4096; ++j)
        hannWindow[(size_t)j] = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * (float)j / 4095.f);
    // V1.4 processor and safe editor are preserved as a rollback path.
    setSize(1338, 661);
    setResizable(true, true);
    setResizeLimits(1159, 573, 1784, 882);
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
    repaint(juce::Rectangle<float>(85.f * getWidth() / baseW, 130.f * getHeight() / baseH,
                                  720.f * getWidth() / baseW, 32.f * getHeight() / baseH).toNearestInt());
}
void SkellyEQRestoredEditor::rebindBand()
{
    freqAttachment.reset(); gainAttachment.reset(); qAttachment.reset();
    typeAttachment.reset(); enabledAttachment.reset();
    soloAttachment.reset(); channelAttachment.reset();
    freqAttachment = std::make_unique<SliderAttachment>(processor.state, bandId(selectedBand, "freq"), frequency);
    gainAttachment = std::make_unique<SliderAttachment>(processor.state, bandId(selectedBand, "gain"), gain);
    qAttachment = std::make_unique<SliderAttachment>(processor.state, bandId(selectedBand, "q"), q);
    if (auto* p = processor.state.getParameter(bandId(selectedBand, "freq")))
        frequency.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
    typeAttachment = std::make_unique<ComboAttachment>(processor.state, bandId(selectedBand, "type"), typeSelect);
    enabledAttachment = std::make_unique<ButtonAttachment>(processor.state, bandId(selectedBand, "enabled"), enabled);
    soloAttachment = std::make_unique<ButtonAttachment>(processor.state, bandId(selectedBand, "solo"), solo);
    channelAttachment = std::make_unique<ComboAttachment>(processor.state, bandId(selectedBand, "channel"), channelSelect);
    formatFrequency(frequency);
    formatDecibels(gain);
    formatQ(q);
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
    // All paint coordinates are in the 1784 x 882 reference space; resized()
    // scales the live controls using precisely the same coordinate system.
    panel(g, {1527.f, 137.f, 204.f, 594.f}, 8.f);
    label(g, "STEREO OUTPUT", 1537.f, 148.f, 183.f, 25.f, 15.f, bright);
    separator(g, 1545.f, 1717.f, 183.f);
    constexpr float top = 195.f, height = 166.f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float x0 = 1560.f + (float)ch * 92.f;
        for (int kind = 0; kind < 2; ++kind)
        {
            const float x = x0 + kind * 26.f;
            const float reading = kind == 0 ? meterPeak[ch] : meterRms[ch];
            const float db = juce::jlimit(-60.f, 6.f,
                juce::Decibels::gainToDecibels(juce::jmax(0.f, reading), -60.f));
            const float level = juce::jlimit(0.f, 1.f, (db + 60.f) / 66.f);
            g.setColour(juce::Colour(0xff0d1c11));
            g.fillRoundedRectangle(x, top, 17.f, height, 3.f);
            for (int segment = 0; segment < 27; ++segment)
            {
                const float segmentLevel = (float)(segment + 1) / 27.f;
                const float y = top + height - (segment + 1) * (height / 27.f);
                const bool lit = segmentLevel <= level;
                juce::Colour c = segment > 24 ? juce::Colour(0xffffc65a)
                               : segment > 20 ? juce::Colour(0xffbbff55)
                               : juce::Colour(0xff42ff5c);
                g.setColour(lit ? c : juce::Colour(0xff193821));
                g.fillRoundedRectangle(x + 2.f, y + 1.f, 13.f, height / 27.f - 1.2f, 1.f);
            }
            g.setColour(juce::Colour(0xff376740));
            g.drawRoundedRectangle(x, top, 17.f, height, 3.f, .9f);
        }
        // Over-level indicator with a short hold; green when clean, amber at clip.
        g.setColour(clipHoldTicks[ch] > 0 ? juce::Colour(0xffff9d43)
                                           : juce::Colour(0xff22482a));
        g.fillEllipse(x0 + 14.f, 177.f, 10.f, 10.f);
        label(g, ch == 0 ? "LEFT" : "RIGHT", x0 - 3.f, 365.f, 69.f, 18.f, 12.f, bright);
        const float peak = juce::Decibels::gainToDecibels(meterPeak[ch], -60.f);
        label(g, juce::String(peak, 1) + " dB", x0 - 11.f, 386.f, 88.f, 19.f, 12.f, bright);
    }
    g.setColour(juce::Colour(0xff9bbca2));
    g.setFont(juce::Font(juce::FontOptions(10.f)));
    for (const int db : {6, 0, -18, -36, -54})
    {
        const float y = top + height - (db + 60.f) / 66.f * height;
        g.drawText(db > 0 ? juce::String("+6") : juce::String(db), 1710, (int)y - 7, 20, 14,
                   juce::Justification::centredLeft, false);
    }
    separator(g, 1545.f, 1717.f, 412.f);
    label(g, "FILTER SHAPE", 1542.f, 414.f, 170.f, 20.f, 13.f, bright);
    label(g, "TYPE", 1539.f, 505.f, 63.f, 17.f, 10.f, green.withAlpha(.8f));
    label(g, "CHANNEL", 1539.f, 534.f, 82.f, 16.f, 10.f, green.withAlpha(.8f));
    label(g, "PHASE", 1539.f, 563.f, 57.f, 17.f, 10.f, green.withAlpha(.8f));
    label(g, "SLOPE", 1539.f, 596.f, 57.f, 17.f, 10.f, green.withAlpha(.8f));
    separator(g, 1545.f, 1717.f, 628.f);
    label(g, "ANALYZER", 1545.f, 631.f, 174.f, 17.f, 12.f, bright);
    label(g, "INTENSITY", 1544.f, 690.f, 174.f, 16.f, 11.f, green);
}

void SkellyEQRestoredEditor::paint(juce::Graphics& g)
{
    g.fillAll(dark);
    const float sx = (float)getWidth() / baseW;
    const float sy = (float)getHeight() / baseH;
    juce::Graphics::ScopedSaveState guard(g);
    g.addTransform(juce::AffineTransform::scale(sx, sy));

    // Blank frame and isolated emblem contain no fake controls or readouts.
    if (skin.isValid())
        g.drawImage(skin, 0.f, 0.f, baseW, baseH, 0, 0, skin.getWidth(), skin.getHeight());
    else
    {
        g.setColour(juce::Colour(0xff0b2511));
        g.fillRect(0.f, 0.f, baseW, baseH);
        g.setColour(green);
        g.drawRoundedRectangle(juce::Rectangle<float>(10.f, 10.f, 1764.f, 860.f), 12.f, 2.f);
    }

    panel(g, {29.f, 24.f, 548.f, 91.f}, 10.f);
    panel(g, {1159.f, 30.f, 571.f, 72.f}, 9.f);
    if (skull.isValid())
    {
        g.setColour(green.withAlpha(.09f));
        g.fillEllipse(784.f, 6.f, 220.f, 127.f);
        g.setOpacity(.98f);
        g.drawImage(skull, 781.f, 0.f, 220.f, 131.f, 0, 0, skull.getWidth(), skull.getHeight());
        g.setOpacity(1.f);
    }
    g.setColour(juce::Colour(0xffa6ffab));
    g.setFont(juce::Font(juce::FontOptions(43.f).withStyle("Bold")));
    g.drawFittedText("SKELLY EQ", juce::Rectangle<int>(62, 33, 477, 54),
                     juce::Justification::centredLeft, 1);
    g.setColour(juce::Colour(0xffa8cdb0));
    g.setFont(juce::Font(juce::FontOptions(14.f).withStyle("Bold")));
    g.drawText("BONE-DEEP PARAMETRIC", 65, 84, 390, 23, juce::Justification::centredLeft);
    label(g, "8 BANDS  /  4096 FFT  /  MINIMUM-PHASE ENGINE", 1110.f, 106.f, 613.f, 19.f, 11.f,
          juce::Colour(0xff82ac89));

    panel(g, {31.f, 129.f, 1491.f, 602.f}, 7.f);
    g.setColour(green.withAlpha(.32f));
    g.fillRect(32.f, 128.f, 1488.f, 2.f);
    g.fillRect(32.f, 729.f, 1488.f, 2.f);

    // Band inspector: selected values are painted once, above the plot.
    const auto bs = processor.bands();
    const auto& band = bs[(size_t)juce::jlimit(0, 7, selectedBand)];
    juce::String freqText = band.freq >= 1000.0
        ? juce::String(band.freq / 1000., 2) + " kHz"
        : juce::String(band.freq, band.freq < 100. ? 1 : 0) + " Hz";
    const juce::String gainText = juce::String(band.gain > 0.05 ? "+" : "") + juce::String(band.gain, 1) + " dB";
    g.setColour(juce::Colour(0xff16271a));
    g.fillRoundedRectangle(88.f, 136.f, 661.f, 25.f, 5.f);
    g.setColour(juce::Colour(0xff397e47));
    g.drawRoundedRectangle(88.f, 136.f, 661.f, 25.f, 5.f, .8f);
    label(g, "BAND " + juce::String(selectedBand + 1), 99.f, 138.f, 92.f, 21.f, 12.f,
          juce::Colour(0xffffc96b));
    label(g, freqText, 201.f, 138.f, 145.f, 21.f, 12.f, bright);
    label(g, gainText, 365.f, 138.f, 129.f, 21.f, 12.f, bright);
    label(g, "Q " + juce::String(band.q, 2), 508.f, 138.f, 106.f, 21.f, 12.f, bright);
    label(g, band.enabled ? "ENABLED" : "BYPASSED", 628.f, 138.f, 110.f, 21.f, 11.f,
          band.enabled ? green : juce::Colour(0xffb0b2ac));
    const int rangeValues[] = {6, 12, 18, 30};
    const int range = rangeValues[juce::jlimit(0, 3,
        (int)processor.state.getRawParameterValue("graphRange")->load())];
    const int step = range == 6 ? 2 : range <= 18 ? 6 : 10;
    for (int db = -range; db <= range; db += step)
        label(g, (db > 0 ? "+" : "") + juce::String(db), 36.f,
              169.f + ((float)range - (float)db) / (2.f * (float)range) * 498.f - 10.f,
              43.f, 21.f, 12.f, db == 0 ? bright : green.withAlpha(.76f));
    const int ticks[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    for (const int frequency : ticks)
    {
        const float px = 99.f + std::log((float)frequency / 20.f) / std::log(1000.f) * 1388.f;
        label(g, frequency >= 1000 ? juce::String(frequency / 1000) + "K" : juce::String(frequency),
              px - 27.f, 682.f, 54.f, 22.f, 13.f, bright);
    }
    label(g, "Hz", 1446.f, 705.f, 50.f, 16.f, 11.f, green);
    label(g, "DRAG NODES TO EDIT  /  SCROLL TO CHANGE Q  /  SHIFT FOR FINE CONTROL",
          126.f, 704.f, 1002.f, 17.f, 11.f, green.withAlpha(.64f));
    drawMeters(g);

    panel(g, {31.f, 746.f, 1491.f, 118.f}, 9.f);
    g.setColour(green.withAlpha(.19f));
    g.fillRect(582.f, 762.f, 1.f, 87.f);
    g.fillRect(1139.f, 762.f, 1.f, 87.f);
    label(g, "BAND SELECT", 83.f, 754.f, 173.f, 16.f, 12.f, bright);
    label(g, "FREQUENCY", 638.f, 844.f, 146.f, 17.f, 13.f, bright);
    label(g, "GAIN", 795.f, 844.f, 146.f, 17.f, 13.f, bright);
    label(g, "Q / WIDTH", 953.f, 844.f, 146.f, 17.f, 13.f, bright);
    label(g, "OUTPUT TRIM", 1179.f, 760.f, 117.f, 18.f, 12.f, green);
    label(g, "BAND ROUTING", 1326.f, 762.f, 177.f, 18.f, 12.f, green);
}

void SkellyEQRestoredEditor::resized()
{
    const float sx = (float)getWidth() / baseW, sy = (float)getHeight() / baseH;
    auto put = [sx, sy](juce::Component& c, float x, float y, float w, float h)
    {
        c.setBounds(juce::Rectangle<float>(x * sx, y * sy, w * sx, h * sy).toNearestInt());
    };
    put(graph, 85, 169, 1416, 498);
    put(about, 1173, 47, 79, 39);
    put(options, 1261, 47, 85, 39);
    put(saveA, 1355, 47, 86, 39);
    put(loadA, 1447, 47, 86, 39);
    put(saveB, 1539, 47, 86, 39);
    put(loadB, 1631, 47, 87, 39);
    // Band toolbar never overlaps the interactive response component.
    put(enabled, 1114, 135, 90, 26);
    put(solo, 1210, 135, 79, 26);
    put(resetCurve, 1297, 135, 195, 26);

    // Dedicated side rail: no dropdowns or phase buttons inside the graph.
    put(typeButtons[0], 1540, 438, 57, 29);
    put(typeButtons[1], 1601, 438, 57, 29);
    put(typeButtons[2], 1662, 438, 57, 29);
    put(typeButtons[3], 1541, 472, 86, 26);
    put(typeButtons[4], 1632, 472, 86, 26);
    put(typeSelect, 1608, 503, 110, 25);
    put(modeSelect, 1625, 531, 93, 26);
    put(phaseSelect, 1604, 560, 114, 28);
    put(slopeSelect, 1605, 593, 113, 27);
    put(analyzeSelect, 1543, 651, 176, 26);
    put(analyzeAmount, 1545, 708, 174, 20);

    // Three precision controls; explicit band tabs offer one-click selection.
    put(bandSelect, 87, 774, 168, 28);
    for (int b = 0; b < 8; ++b)
        put(bandTabs[(size_t)b], 79.f + 47.f * b, 811.f, 42.f, 31.f);
    put(previous, 465, 773, 42, 30);
    put(next, 516, 773, 42, 30);
    put(frequency, 642, 752, 135, 89);
    put(gain, 798, 752, 135, 89);
    put(q, 954, 752, 135, 89);
    put(output, 1182, 779, 106, 77);
    put(channelSelect, 1324, 791, 179, 33);
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
        const float currentPeak = processor.peaks[ch].load(std::memory_order_relaxed);
        const float currentRms = processor.rms[ch].load(std::memory_order_relaxed);
        meterPeak[ch] = juce::jmax(currentPeak, meterPeak[ch] * .78f);
        meterRms[ch] = juce::jmax(currentRms, meterRms[ch] * .78f);
        clipHoldTicks[ch] = currentPeak >= 1.f ? 45 : juce::jmax(0, clipHoldTicks[ch] - 1);
    }
    const int selectedType = (int)processor.state.getRawParameterValue(bandId(selectedBand, "type"))->load();
    const int types[] = {3, 0, 4, 5, 6};
    for (int i = 0; i < 5; ++i)
        typeButtons[i].setToggleState(selectedType == types[i], juce::dontSendNotification);
    graph.repaint();
    const float sx = (float)getWidth() / baseW, sy = (float)getHeight() / baseH;
    repaint(juce::Rectangle<float>(1526.f * sx, 140.f * sy, 205.f * sx, 278.f * sy).toNearestInt());
    repaint(juce::Rectangle<float>(87.f * sx, 134.f * sy, 663.f * sx, 31.f * sy).toNearestInt());
}
