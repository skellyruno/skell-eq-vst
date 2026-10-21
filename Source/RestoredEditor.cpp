#include "RestoredEditor.h"
#include "CrashTrace.h"
#include "Theme.h"
#include "dsp/BiquadCascade.h"
#include <BinaryData.h>
#include <cmath>
#include <complex>

namespace
{
constexpr float baseW = 1784.0f;
constexpr float baseH = 882.0f;
const juce::Colour green(0xff39ff3a), bright(0xff98ff8c), dark(0xff061008);

void configureDial(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 98, 21);
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
    graphTexture = juce::ImageFileFormat::loadFrom(BinaryData::graph_bg_png,
                                                   BinaryData::graph_bg_pngSize);
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
        const bool ownGesture = p != gestureParam;
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
    for (int b = 7; b >= 0; --b)
        if (bands[(size_t)b].enabled && position.getDistanceFrom(
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
}

void SkellyEQRestoredEditor::ResponseGraph::paint(juce::Graphics& g)
{
    const float w = (float)getWidth(), h = (float)getHeight();
    if (w < 2.f || h < 2.f) return;
    g.fillAll(juce::Colour(0xff030a05));
    if (graphTexture.isValid())
    {
        g.setOpacity(0.28f);
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
    g.strokePath(cachedResponse, juce::PathStrokeType(13.f));
    g.setColour(bright.withAlpha(.4f));
    g.strokePath(cachedResponse, juce::PathStrokeType(5.f));
    g.setColour(green);
    g.strokePath(cachedResponse, juce::PathStrokeType(2.6f));
    for (int b = 0; b < 8; ++b)
    {
        const auto& band = bands[(size_t)b];
        if (!band.enabled) continue;
        const float x = xForFrequency((float)band.freq), y = yForGain((float)band.gain);
        g.setColour(green.withAlpha(.20f));
        g.fillEllipse(x - 15.f, y - 15.f, 30.f, 30.f);
        g.setColour(b == selected ? juce::Colour(0xffffc21a) : bright);
        g.drawEllipse(x - 9.f, y - 9.f, 18.f, 18.f, 2.5f);
        g.setColour(dark);
        g.fillEllipse(x - 5.f, y - 5.f, 10.f, 10.f);
        g.setColour(green);
        g.fillEllipse(x - 2.f, y - 2.f, 4.f, 4.f);
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
    beginDragParam(param(dragged, e.mods.isAltDown() ? "q" : "freq"));
    if (onSelection) onSelection(dragged);
    else selected = dragged;
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
        setParam(band, "enabled", 0.f);
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
    // From the user's supplied Skelly EQ design. Pure decoration; interactive
    // graph and controls are independently rendered and cover baked screenshot UI.
    skin = juce::ImageFileFormat::loadFrom(BinaryData::reference_skin_png,
                                           BinaryData::reference_skin_pngSize);
    selectedBand = juce::jlimit(0, 7,
        (int)processor.state.state.getProperty("selectedBand", 0));
    setLookAndFeel(&look);
    addAndMakeVisible(graph);
    graph.onSelection = [this](int b) { selectBand(b); };

    configureChoice(bandSelect, {"BAND 1", "BAND 2", "BAND 3", "BAND 4",
                                  "BAND 5", "BAND 6", "BAND 7", "BAND 8"});
    configureChoice(typeSelect, {"Peak", "Low Shelf", "High Shelf", "Low Cut",
                                  "High Cut", "Notch", "Band Pass", "Tilt"});
    configureChoice(phaseSelect, {"LINEAR*", "MIN PHASE", "ZERO LATENCY"});
    configureChoice(slopeSelect, {"12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"});
    configureChoice(modeSelect, {"L/R", "M/S"});
    for (auto* c : {&bandSelect, &typeSelect, &phaseSelect, &slopeSelect, &modeSelect})
        addAndMakeVisible(*c);

    for (auto* s : {&frequency, &gain, &q, &output})
    {
        configureDial(*s);
        addAndMakeVisible(*s);
    }
    addAndMakeVisible(enabled);
    for (auto* b : {&options, &about, &previous, &next})
        addAndMakeVisible(*b);
    enabled.setColour(juce::ToggleButton::textColourId, bright);
    about.setButtonText("INFO");
    about.setTooltip("Skelly EQ v1.5 / staged GUI restoration / minimum-phase audio engine");
    about.setEnabled(false); // No asynchronous host dialogs in this release.
    options.setTooltip("Cycle graph dB display range");
    options.onClick = [this]
    {
        const auto nextRange = ((int)processor.state.getRawParameterValue("graphRange")->load() + 1) % 4;
        setGlobal("graphRange", (float)nextRange);
    };
    previous.onClick = [this] { selectBand((selectedBand + 7) % 8); };
    next.onClick = [this] { selectBand((selectedBand + 1) % 8); };

    const char* typeNames[] = {"LOW CUT", "PEAK", "HIGH CUT", "NOTCH", "BAND"};
    const int typeIds[] = {3, 0, 4, 5, 6};
    for (int i = 0; i < 5; ++i)
    {
        typeButtons[i].setButtonText(typeNames[i]);
        typeButtons[i].onClick = [this, type = typeIds[i]] { setBandType(type); };
        addAndMakeVisible(typeButtons[i]);
    }
    outputAttachment = std::make_unique<SliderAttachment>(processor.state, "outputGain", output);
    phaseAttachment = std::make_unique<ComboAttachment>(processor.state, "phaseMode", phaseSelect);
    slopeAttachment = std::make_unique<ComboAttachment>(processor.state, "slope", slopeSelect);
    modeAttachment = std::make_unique<ComboAttachment>(processor.state, "stereoMode", modeSelect);
    bandSelect.onChange = [this] { selectBand(bandSelect.getSelectedId() - 1); };
    bandSelect.setSelectedId(selectedBand + 1, juce::dontSendNotification);
    rebindBand();

    buildTag.setText("V1.5 RESTORED / FFT OFF / MIN PHASE DSP",
                     juce::dontSendNotification);
    buildTag.setFont(juce::Font(juce::FontOptions(11.0f)));
    buildTag.setColour(juce::Label::textColourId, bright);
    buildTag.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(buildTag);

    // V1.4 processor and state-loading code remain untouched.
    setSize(1248, 617);
    setResizable(true, true);
    setResizeLimits(892, 441, 1784, 882);
    if (auto* constraint = getConstrainer())
        constraint->setFixedAspectRatio((double)baseW / (double)baseH);
    startTimerHz(15); // Small meter/automation refresh only; NO FFT timer.
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
    for (auto& b : typeButtons) b.onClick = nullptr;
    freqAttachment.reset(); gainAttachment.reset(); qAttachment.reset();
    outputAttachment.reset(); typeAttachment.reset(); phaseAttachment.reset();
    slopeAttachment.reset(); modeAttachment.reset(); enabledAttachment.reset();
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
    freqAttachment = std::make_unique<SliderAttachment>(processor.state, bandId(selectedBand, "freq"), frequency);
    gainAttachment = std::make_unique<SliderAttachment>(processor.state, bandId(selectedBand, "gain"), gain);
    qAttachment = std::make_unique<SliderAttachment>(processor.state, bandId(selectedBand, "q"), q);
    typeAttachment = std::make_unique<ComboAttachment>(processor.state, bandId(selectedBand, "type"), typeSelect);
    enabledAttachment = std::make_unique<ButtonAttachment>(processor.state, bandId(selectedBand, "enabled"), enabled);
}
void SkellyEQRestoredEditor::setGlobal(const juce::String& key, float value)
{
    if (auto* p = processor.state.getParameter(key))
    {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(value)); p->endChangeGesture();
    }
    graph.repaint();
}
void SkellyEQRestoredEditor::setBandType(int type)
{
    setGlobal(bandId(selectedBand, "type"), (float)type);
}

void SkellyEQRestoredEditor::drawMeters(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff020b05));
    g.fillRoundedRectangle(1540.f, 127.f, 174.f, 619.f, 5.f);
    g.setColour(juce::Colour(0xff1b7426));
    g.drawRoundedRectangle(1540.f, 127.f, 174.f, 619.f, 5.f, 1.3f);
    label(g, "OUTPUT", 1542.f, 138.f, 80.f, 24.f, 15.f);
    for (int n = 0; n < 4; ++n)
    {
        const float x = 1559.f + n * 37.f;
        const float reading = n % 2 == 0 ? meterPeak[n / 2] : meterRms[n / 2];
        const float db = juce::Decibels::gainToDecibels(reading, -60.f);
        const float amount = juce::jlimit(0.f, 1.f, (db + 60.f) / 72.f);
        g.setColour(juce::Colour(0xff0b2410));
        g.fillRect(x, 177.f, 22.f, 242.f);
        g.setColour(green.withAlpha(n % 2 == 0 ? 1.f : .58f));
        g.fillRect(x, 419.f - 242.f * amount, 22.f, 242.f * amount);
        g.setColour(bright.withAlpha(.45f));
        g.drawRect(x, 177.f, 22.f, 242.f, 1.f);
    }
    label(g, "L      RMS      R     RMS", 1546.f, 420.f, 161.f, 20.f, 10.f);
    label(g, "OUTPUT GAIN", 1541.f, 661.f, 168.f, 19.f, 12.f);
}

void SkellyEQRestoredEditor::paint(juce::Graphics& g)
{
    g.fillAll(dark);
    const float sx = (float)getWidth() / baseW;
    const float sy = (float)getHeight() / baseH;
    juce::Graphics::ScopedSaveState guard(g);
    g.addTransform(juce::AffineTransform::scale(sx, sy));
    if (skin.isValid())
        g.drawImage(skin, 0.f, 0.f, baseW, baseH, 0, 0, skin.getWidth(), skin.getHeight());
    else
    {
        g.setColour(green);
        g.drawRect(juce::Rectangle<float>(10, 10, 1764, 862), 3.f);
        label(g, "SKELLY EQ", 80.f, 40.f, 380.f, 45.f, 36.f);
    }
    drawMeters(g);
    // Mask static screenshot knobs with the live controls' backgrounds.
    for (float x : {635.f, 790.f, 950.f})
    {
        g.setColour(juce::Colour(0xff061008));
        g.fillRoundedRectangle(x, 739.f, 122.f, 107.f, 11.f);
    }
    label(g, "FREQ", 635.f, 832.f, 120.f, 23.f, 18.f);
    label(g, "GAIN", 790.f, 832.f, 120.f, 23.f, 18.f);
    label(g, "Q", 950.f, 832.f, 120.f, 23.f, 18.f);
    const int rangeValues[] = {6, 12, 18, 30};
    const int range = rangeValues[juce::jlimit(0, 3,
        (int)processor.state.getRawParameterValue("graphRange")->load())];
    for (int db = -range; db <= range; db += range <= 18 ? (range == 6 ? 2 : 6) : 10)
        label(g, (db > 0 ? "+" : "") + juce::String(db),
              29.f, 137.f + ((float)range - (float)db) / (2.f * (float)range) * 545.f - 11.f,
              47.f, 22.f, 13.f, bright);
    const int ticks[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    for (int f : ticks)
    {
        const float x = 83.f + std::log((float)f / 20.f) / std::log(1000.f) * 1450.f;
        label(g, f >= 1000 ? juce::String(f / 1000) + "K" : juce::String(f),
              x - 27.f, 684.f, 54.f, 26.f, 14.f, bright);
    }
    label(g, "FILTER", 1545.f, 445.f, 165.f, 17.f, 12.f);
    label(g, "BAND", 1390.f, 774.f, 115.f, 20.f, 13.f);
}

void SkellyEQRestoredEditor::resized()
{
    const float sx = (float)getWidth() / baseW, sy = (float)getHeight() / baseH;
    auto put = [sx, sy](juce::Component& c, float x, float y, float w, float h)
    {
        c.setBounds(juce::Rectangle<float>(x * sx, y * sy, w * sx, h * sy).toNearestInt());
    };
    put(graph, 83, 137, 1450, 545);
    put(options, 1485, 55, 97, 41);
    put(about, 1368, 55, 109, 41);
    put(phaseSelect, 1114, 520, 272, 35);
    put(slopeSelect, 1160, 562, 226, 35);
    put(modeSelect, 1544, 616, 165, 34);
    put(typeSelect, 1544, 576, 165, 34);
    put(typeButtons[0], 1544, 473, 55, 55);
    put(typeButtons[1], 1601, 473, 55, 55);
    put(typeButtons[2], 1658, 473, 55, 55);
    put(typeButtons[3], 1544, 530, 81, 39);
    put(typeButtons[4], 1629, 530, 81, 39);
    put(enabled, 1401, 795, 107, 36);
    put(bandSelect, 1363, 831, 156, 31);
    put(frequency, 630, 745, 129, 86);
    put(gain, 785, 745, 129, 86);
    put(q, 945, 745, 129, 86);
    put(output, 1590, 685, 90, 72);
    put(previous, 532, 767, 47, 41);
    put(next, 1120, 767, 47, 41);
    put(buildTag, 82, 99, 381, 20);
}

void SkellyEQRestoredEditor::timerCallback()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        const float currentPeak = processor.peaks[ch].load(std::memory_order_relaxed);
        const float currentRms  = processor.rms[ch].load(std::memory_order_relaxed);
        meterPeak[ch] = juce::jmax(currentPeak, meterPeak[ch] * .79f);
        meterRms[ch] = juce::jmax(currentRms, meterRms[ch] * .79f);
    }
    // No real-time FFT until the restored editor is validated in FL Studio.
    graph.repaint();
    repaint();
}
