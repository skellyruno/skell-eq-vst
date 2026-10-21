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
    g.setColour(juce::Colour(0xff051009));
    g.fillRoundedRectangle(r, corner);
    g.setColour(juce::Colour(0xff2f7134).withAlpha(.75f));
    g.drawRoundedRectangle(r, corner, 1.1f);
}

void configureDial(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 112, 23);
    s.setDoubleClickReturnValue(true, 0.0);
    s.setColour(juce::Slider::textBoxTextColourId, bright);
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff071109));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff1c5924));
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
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff061307));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff205e25));
    setColour(juce::TextButton::textColourOffId, bright);
    setColour(juce::TextButton::textColourOnId, bright);
    setColour(juce::ToggleButton::textColourId, bright);
}

void SkellyEQRestoredEditor::NeonLook::drawRotarySlider(juce::Graphics& g, int x, int y,
    int width, int height, float proportion, float startAngle, float endAngle, juce::Slider&)
{
    const float r = 0.43f * (float)juce::jmin(width, height);
    const auto cx = x + width * 0.5f, cy = y + height * 0.5f;
    const auto a = startAngle + proportion * (endAngle - startAngle);
    g.setColour(juce::Colour(0xff0d1c11));
    g.fillEllipse(cx - r, cy - r, r * 2.f, r * 2.f);
    g.setColour(juce::Colour(0xff366f3d));
    g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.5f);
    juce::Path track;
    track.addCentredArc(cx, cy, r + 2.f, r + 2.f, 0, startAngle, a, true);
    g.setColour(green.withAlpha(0.25f));
    g.strokePath(track, juce::PathStrokeType(7.0f));
    g.setColour(bright);
    g.strokePath(track, juce::PathStrokeType(2.8f));
    juce::Path needle;
    needle.startNewSubPath(cx, cy);
    needle.lineTo(cx + std::sin(a) * r * 0.7f,
                  cy - std::cos(a) * r * 0.7f);
    g.strokePath(needle, juce::PathStrokeType(2.7f));
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
    return std::log(juce::jlimit(20.f, 20000.f, f) / 20.f) / std::log(1000.f)
           * (float)getWidth();
}
float SkellyEQRestoredEditor::ResponseGraph::frequencyForX(float x) const
{
    return 20.f * std::pow(1000.f, juce::jlimit(0.f, 1.f, x / (float)juce::jmax(1, getWidth())));
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
                yForGain((float)bands[(size_t)b].gain)}) < 18.0f)
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
    if (hasSpectrum)
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
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff38fa46).withAlpha(.27f), 0.f, h * .35f,
                                              juce::Colour(0xff38fa46).withAlpha(.01f), 0.f, h, false));
        g.fillPath(fill);
        g.setColour(juce::Colour(0xff70ff70).withAlpha(.55f));
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
    g.setColour(green.withAlpha(.13f));
    g.strokePath(cachedResponse, juce::PathStrokeType(10.f));
    g.setColour(bright.withAlpha(.4f));
    g.strokePath(cachedResponse, juce::PathStrokeType(5.f));
    g.setColour(green);
    g.strokePath(cachedResponse, juce::PathStrokeType(2.6f));
    bool anyEnabled = false;
    for (int b = 0; b < 8; ++b)
    {
        const auto& band = bands[(size_t)b];
        anyEnabled |= band.enabled;
        const float x = xForFrequency((float)band.freq);
        const float y = yForGain((float)band.gain);
        // Dim ring = bypassed, bright ring = active, amber = selected.
        // Always render saved band positions, even if all bands are disabled.
        if (band.enabled)
        {
            g.setColour(green.withAlpha(.21f));
            g.fillEllipse(x - 15.f, y - 15.f, 30.f, 30.f);
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
        if (getWidth() >= 1000 && band.enabled)
        {
            g.setColour(juce::Colour(0xffe5ffe7));
            g.setFont(juce::Font(juce::FontOptions(10.f).withStyle("Bold")));
            g.drawText(juce::String(b + 1), juce::Rectangle<int>((int)x - 7, (int)y - 8, 14, 16),
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
    // The selection controls are JUCE children; paint an inset beneath them.
    g.setColour(juce::Colour(0xff020b05).withAlpha(.94f));
    g.fillRoundedRectangle(w - 477.f, 9.f, 465.f, 48.f, 6.f);
    g.setColour(green.withAlpha(.38f));
    g.drawRoundedRectangle(w - 477.f, 9.f, 465.f, 48.f, 6.f, 1.f);
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
    configureChoice(typeSelect, {"Peak", "Low Shelf", "High Shelf", "Low Cut",
                                  "High Cut", "Notch", "Band Pass", "Tilt"});
    configureChoice(phaseSelect, {"LINEAR (N/A)", "MIN PHASE", "ZERO LATENCY"});
    configureChoice(slopeSelect, {"12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"});
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
    frequency.textFromValueFunction = [](double v)
    {
        return v >= 1000.0 ? juce::String(v / 1000.0, v >= 10000.0 ? 1 : 2) + " kHz"
                           : juce::String(v, v < 100.0 ? 1 : 0) + " Hz";
    };
    frequency.valueFromTextFunction = [](const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        return t.getDoubleValue() * (t.contains("khz") || t.endsWithChar('k') ? 1000.0 : 1.0);
    };
    gain.textFromValueFunction = [](double v)
    {
        return juce::String(v > 0.0 ? "+" : "") + juce::String(v, 1) + " dB";
    };
    gain.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
    q.textFromValueFunction = [](double v) { return juce::String(v, 2); };
    q.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
    analyzeAmount.textFromValueFunction = [](double v) { return juce::String(v, 0) + " %"; };
    analyzeAmount.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
    output.textFromValueFunction = gain.textFromValueFunction;
    output.valueFromTextFunction = gain.valueFromTextFunction;
    // Double click returns to the correct default for the currently selected
    // band; a hard-coded 0 would otherwise reset every frequency to 20 Hz.
    frequency.setDoubleClickReturnValue(false, 0.0);
    q.setDoubleClickReturnValue(true, 1.0);
    gain.setDoubleClickReturnValue(true, 0.0);
    output.setDoubleClickReturnValue(true, 0.0);
    analyzeAmount.setDoubleClickReturnValue(true, 100.0);
    analyzeAmount.setSliderStyle(juce::Slider::LinearHorizontal);
    analyzeAmount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
    addAndMakeVisible(enabled);
    addAndMakeVisible(solo);
    solo.setColour(juce::ToggleButton::textColourId, bright);
    for (auto* b : {&options, &about, &previous, &next, &resetCurve,
                    &saveA, &loadA, &saveB, &loadB})
        addAndMakeVisible(*b);
    enabled.setColour(juce::ToggleButton::textColourId, bright);
    about.setTooltip("Skelly EQ 1.7 | Stable DSP, original GUI, FFT on editor thread");
    about.setButtonText("v1.7.0");
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

    const char* typeNames[] = {"LOW CUT", "PEAK", "HIGH CUT", "NOTCH", "BAND"};
    const int typeIds[] = {3, 0, 4, 5, 6};
    for (int i = 0; i < 5; ++i)
    {
        typeButtons[i].setButtonText(typeNames[i]);
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

    buildTag.setText("V1.7 | SPECTRUM",
                     juce::dontSendNotification);
    buildTag.setFont(juce::Font(juce::FontOptions(11.0f)));
    buildTag.setColour(juce::Label::textColourId, bright);
    buildTag.setJustificationType(juce::Justification::centred);
    // Version appears in the header controls, never as duplicated bitmap text.
    buildTag.setVisible(false);

    // Avoid showing stale spectrum from a previous editor session.
    processor.analyzer.discardPending();
    // V1.4 processor and safe editor are preserved as a rollback path.
    setSize(1248, 617);
    setResizable(true, true);
    setResizeLimits(892, 441, 1784, 882);
    if (auto* constraint = getConstrainer())
        constraint->setFixedAspectRatio((double)baseW / (double)baseH);
    startTimerHz(24); // FFT runs on message thread, not DSP thread.
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
    graph.repaint();
}
void SkellyEQRestoredEditor::rebindBand()
{
    freqAttachment.reset(); gainAttachment.reset(); qAttachment.reset();
    typeAttachment.reset(); enabledAttachment.reset();
    soloAttachment.reset(); channelAttachment.reset();
    freqAttachment = std::make_unique<SliderAttachment>(processor.state, bandId(selectedBand, "freq"), frequency);
    gainAttachment = std::make_unique<SliderAttachment>(processor.state, bandId(selectedBand, "gain"), gain);
    qAttachment = std::make_unique<SliderAttachment>(processor.state, bandId(selectedBand, "q"), q);
    typeAttachment = std::make_unique<ComboAttachment>(processor.state, bandId(selectedBand, "type"), typeSelect);
    enabledAttachment = std::make_unique<ButtonAttachment>(processor.state, bandId(selectedBand, "enabled"), enabled);
    soloAttachment = std::make_unique<ButtonAttachment>(processor.state, bandId(selectedBand, "solo"), solo);
    channelAttachment = std::make_unique<ComboAttachment>(processor.state, bandId(selectedBand, "channel"), channelSelect);
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
    panel(g, {1526.f, 140.f, 205.f, 580.f});
    label(g, "OUTPUT", 1538.f, 151.f, 170.f, 25.f, 16.f, bright);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float x = 1555.f + (float)ch * 84.f;
        const float readings[] = {meterPeak[ch], meterRms[ch]};
        for (int kind = 0; kind < 2; ++kind)
        {
            const float xx = x + kind * 29.f;
            const float db = juce::Decibels::gainToDecibels(readings[kind], -60.f);
            const float amount = juce::jlimit(0.f, 1.f, (db + 60.f) / 66.f);
            g.setColour(juce::Colour(0xff101c12));
            g.fillRoundedRectangle(xx, 198.f, 21.f, 211.f, 3.f);
            g.setColour(kind == 0 ? green : green.withAlpha(.56f));
            g.fillRect(xx + 2.f, 407.f - 207.f * amount, 17.f, 207.f * amount);
            g.setColour(green.withAlpha(.35f));
            g.drawRect(xx, 198.f, 21.f, 211.f, 1.f);
        }
        label(g, ch == 0 ? "L" : "R", x - 4.f, 414.f, 58.f, 17.f, 13.f, bright);
        const float peak = juce::Decibels::gainToDecibels(meterPeak[ch], -60.f);
        label(g, juce::String(peak, 1) + " dB", x - 10.f, 434.f, 75.f, 18.f, 12.f, bright);
    }
    label(g, "FILTER", 1536.f, 448.f, 186.f, 21.f, 14.f, bright);
    label(g, "ANALYZER", 1536.f, 617.f, 186.f, 18.f, 12.f, green);
    label(g, "AMOUNT", 1536.f, 666.f, 186.f, 15.f, 12.f, green);
}

void SkellyEQRestoredEditor::paint(juce::Graphics& g)
{
    g.fillAll(dark);
    const float sx = (float)getWidth() / baseW;
    const float sy = (float)getHeight() / baseH;
    juce::Graphics::ScopedSaveState guard(g);
    g.addTransform(juce::AffineTransform::scale(sx, sy));

    // Clean PNG has no printed controls or plot labels. This eliminates every
    // screenshot-vs-live-control collision from v1.5/v1.6.
    if (skin.isValid())
        g.drawImage(skin, 0.f, 0.f, baseW, baseH, 0, 0, skin.getWidth(), skin.getHeight());
    else
    {
        g.setColour(juce::Colour(0xff0d2312));
        g.fillRect(0.f, 0.f, baseW, baseH);
        g.setColour(green);
        g.drawRoundedRectangle(juce::Rectangle<float>(10.f, 10.f, 1764.f, 860.f), 12.f, 2.f);
    }

    // Header plates with crisp labels; emblem is a separate decorative resource.
    panel(g, {30.f, 24.f, 565.f, 92.f}, 10.f);
    panel(g, {1155.f, 30.f, 579.f, 75.f}, 9.f);
    if (skull.isValid())
    {
        g.setColour(green.withAlpha(.13f));
        g.fillEllipse(783.f, 11.f, 221.f, 127.f);
        g.setOpacity(.98f);
        g.drawImage(skull, 777.f, 0.f, 231.f, 134.f, 0, 0, skull.getWidth(), skull.getHeight());
        g.setOpacity(1.f);
    }
    g.setColour(bright);
    g.setFont(juce::Font(juce::FontOptions(42.f).withStyle("Bold")));
    g.drawFittedText("SKELLY EQ", juce::Rectangle<int>(66, 35, 475, 53), juce::Justification::centredLeft, 1);
    g.setColour(juce::Colour(0xff9dbfa4));
    g.setFont(juce::Font(juce::FontOptions(14.f).withStyle("Bold")));
    g.drawText("BONE-DEEP PARAMETRIC", 67, 85, 420, 23, juce::Justification::centredLeft);
    label(g, "MIN PHASE DSP  |  8 BANDS  |  LIVE ANALYZER", 1128.f, 103.f, 614.f, 22.f, 12.f, juce::Colour(0xff7da383));

    panel(g, {31.f, 130.f, 1491.f, 599.f}, 7.f);
    // Graph itself is a child component at 85,158 / 1416 x 509.
    const int rangeValues[] = {6, 12, 18, 30};
    const int range = rangeValues[juce::jlimit(0, 3,
        (int)processor.state.getRawParameterValue("graphRange")->load())];
    const int step = range == 6 ? 2 : range <= 18 ? 6 : 10;
    for (int db = -range; db <= range; db += step)
        label(g, (db > 0 ? "+" : "") + juce::String(db),
              36.f, 158.f + ((float)range - (float)db) / (2.f * (float)range) * 509.f - 10.f,
              43.f, 21.f, 13.f, db == 0 ? bright : green.withAlpha(.75f));
    const int ticks[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    for (const int f : ticks)
    {
        const float x = 85.f + std::log((float)f / 20.f) / std::log(1000.f) * 1416.f;
        label(g, f >= 1000 ? juce::String(f / 1000) + "K" : juce::String(f),
              x - 27.f, 682.f, 54.f, 22.f, 14.f, bright);
    }
    label(g, "Hz", 1446.f, 707.f, 50.f, 15.f, 11.f, green);
    drawMeters(g);

    panel(g, {32.f, 747.f, 1473.f, 116.f}, 9.f);
    // Main control section: no screenshot-derived text beneath sliders.
    label(g, "FREQUENCY", 640.f, 839.f, 136.f, 20.f, 16.f, bright);
    label(g, "GAIN", 796.f, 839.f, 136.f, 20.f, 16.f, bright);
    label(g, "Q / WIDTH", 951.f, 839.f, 136.f, 20.f, 16.f, bright);
    label(g, "OUTPUT", 1176.f, 760.f, 108.f, 17.f, 12.f, green);
    label(g, "BAND CHANNEL", 1334.f, 771.f, 150.f, 16.f, 12.f, green);
    label(g, "SELECT BAND", 79.f, 765.f, 340.f, 20.f, 12.f, green);
}

void SkellyEQRestoredEditor::resized()
{
    const float sx = (float)getWidth() / baseW, sy = (float)getHeight() / baseH;
    auto put = [sx, sy](juce::Component& c, float x, float y, float w, float h)
    {
        c.setBounds(juce::Rectangle<float>(x * sx, y * sy, w * sx, h * sy).toNearestInt());
    };
    put(graph, 85, 158, 1416, 509);
    // Header navigation: no overlap with the skull or title.
    put(about, 1171, 46, 86, 44);
    put(options, 1264, 46, 88, 44);
    put(saveA, 1362, 46, 85, 44);
    put(loadA, 1454, 46, 85, 44);
    put(saveB, 1546, 46, 85, 44);
    put(loadB, 1638, 46, 85, 44);
    // Band controls in top graph overlay.
    put(enabled, 1140, 171, 100, 33);
    put(solo, 1240, 171, 79, 33);
    put(resetCurve, 1321, 171, 166, 33);
    put(phaseSelect, 1050, 605, 236, 38);
    put(slopeSelect, 1296, 605, 192, 38);
    // Meter/filter panel: five filters in two rows, nothing obscured.
    put(typeButtons[0], 1537, 475, 60, 42);
    put(typeButtons[1], 1599, 475, 60, 42);
    put(typeButtons[2], 1662, 475, 60, 42);
    put(typeButtons[3], 1537, 519, 89, 33);
    put(typeButtons[4], 1630, 519, 92, 33);
    put(typeSelect, 1538, 557, 184, 29);
    put(modeSelect, 1538, 593, 184, 29);
    put(analyzeSelect, 1538, 635, 184, 28);
    put(analyzeAmount, 1538, 683, 184, 30);
    // Bottom control strip.
    put(bandSelect, 101, 795, 175, 33); // Prefer bottom strip over an overlay in the graph.
    put(frequency, 642, 753, 130, 83);
    put(gain, 799, 753, 130, 83);
    put(q, 953, 753, 130, 83);
    put(output, 1178, 779, 107, 72);
    put(channelSelect, 1334, 797, 150, 32);
    put(previous, 303, 793, 60, 41);
    put(next, 372, 793, 60, 41);
}

void SkellyEQRestoredEditor::timerCallback()
{
    // Processing thread only writes to the existing SPSC ring. One consumer
    // (this editor timer) drains it; the FFT owns preallocated work buffers.
    const auto mode = (int) processor.state.getRawParameterValue("analyzeMode")->load();
    if (mode != 0)
    {
        std::array<float, 2048> popped{};
        for (int tries = 0; tries < 8; ++tries)
        {
            const int n = processor.analyzer.pop(popped.data(), (int)popped.size());
            if (n <= 0) break;
            for (int i = 0; i < n; ++i)
            {
                fftInput[(size_t)fftFill++] = popped[(size_t)i];
                if (fftFill == 4096)
                {
                    fftFill = 0;
                    std::fill(fftWork.begin(), fftWork.end(), 0.f);
                    for (int j = 0; j < 4096; ++j)
                    {
                        const float phase = juce::MathConstants<float>::twoPi * (float)j / 4095.f;
                        fftWork[(size_t)j] = fftInput[(size_t)j] * (0.5f - 0.5f * std::cos(phase));
                    }
                    fft.performFrequencyOnlyForwardTransform(fftWork.data());
                    const double sr = processor.sampleRateHz();
                    const float transparency = processor.state.getRawParameterValue("analyzeAmount")->load() / 100.f;
                    for (int k = 0; k < 256; ++k)
                    {
                        const float x = (float)k / 255.f;
                        const float freq = 20.f * std::pow(1000.f, x);
                        const int bin = juce::jlimit(1, 2047, (int)std::round(freq * 4096.f / (float)sr));
                        const float amplitude = fftWork[(size_t)bin] / 1024.f;
                        const float db = juce::jlimit(-90.f, 0.f, juce::Decibels::gainToDecibels(amplitude, -90.f));
                        const float scaled = -90.f + (db + 90.f) * transparency;
                        graph.spectrumDb[(size_t)k] = juce::jmax(scaled, graph.spectrumDb[(size_t)k] * .63f + scaled * .37f);
                    }
                    graph.hasSpectrum = true;
                }
            }
            if (n < (int)popped.size()) break;
        }
    }
    else
    {
        // Empty the FIFO when analysis is disabled to avoid stale spectrum
        // upon re-enabling it in a later FL Studio session.
        std::array<float, 2048> discarded{};
        while (processor.analyzer.pop(discarded.data(), (int)discarded.size()) > 0) {}
        graph.hasSpectrum = false;
        fftFill = 0;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        const float currentPeak = processor.peaks[ch].load(std::memory_order_relaxed);
        const float currentRms = processor.rms[ch].load(std::memory_order_relaxed);
        meterPeak[ch] = juce::jmax(currentPeak, meterPeak[ch] * .78f);
        meterRms[ch] = juce::jmax(currentRms, meterRms[ch] * .78f);
    }
    const int selectedType = (int)processor.state.getRawParameterValue(bandId(selectedBand, "type"))->load();
    const int types[] = {3, 0, 4, 5, 6};
    for (int i = 0; i < 5; ++i)
        typeButtons[i].setToggleState(selectedType == types[i], juce::dontSendNotification);
    graph.repaint();
    const float sx = (float)getWidth() / baseW, sy = (float)getHeight() / baseH;
    repaint(juce::Rectangle<float>(1526.f * sx, 140.f * sy, 205.f * sx, 330.f * sy).toNearestInt());
}
