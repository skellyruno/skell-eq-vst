#include "StableEditor.h"
#include "CrashTrace.h"

static juce::String stableBandParameter(int band, const char* suffix)
{
    return "b" + juce::String(band + 1) + "_" + suffix;
}

void SkellyEQStableEditor::prepareRotary(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 108, 24);
}

void SkellyEQStableEditor::prepareLabel(juce::Label& label, const juce::String& name)
{
    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
}

void SkellyEQStableEditor::prepareChoice(juce::ComboBox& combo, const juce::StringArray& options)
{
    combo.addItemList(options, 1);
    combo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff17251a));
    combo.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    combo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff28692d));
}

SkellyEQStableEditor::SkellyEQStableEditor(SkellyEQAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    SkellyTrace::checkpoint("stable-editor: constructor ENTER");
    title.setText("SKELLY EQ", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(30.0f).withStyle("Bold")));
    title.setColour(juce::Label::textColourId, juce::Colour(0xff39ff3a));
    buildLabel.setText("V1.4 SAFE EDITOR - FL STUDIO CRASH ISOLATION", juce::dontSendNotification);
    buildLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    buildLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(title);
    addAndMakeVisible(buildLabel);

    prepareLabel(bandLabel, "BAND");
    prepareLabel(typeLabel, "FILTER TYPE");
    prepareLabel(phaseLabel, "PHASE");
    prepareLabel(slopeLabel, "CUT SLOPE");
    prepareLabel(stereoLabel, "STEREO MODE");
    prepareLabel(freqLabel, "FREQUENCY");
    prepareLabel(gainLabel, "GAIN");
    prepareLabel(qLabel, "Q");
    prepareLabel(outputLabel, "OUTPUT");
    for (juce::Label* label : {&bandLabel, &typeLabel, &phaseLabel, &slopeLabel,
                               &stereoLabel, &freqLabel, &gainLabel, &qLabel, &outputLabel})
        addAndMakeVisible(*label);

    prepareChoice(bandSelect, {"Band 1", "Band 2", "Band 3", "Band 4", "Band 5", "Band 6", "Band 7", "Band 8"});
    prepareChoice(typeSelect, {"Peak", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt"});
    prepareChoice(phaseSelect, {"Linear*", "Min Phase", "Zero Latency"});
    prepareChoice(slopeSelect, {"12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"});
    prepareChoice(stereoSelect, {"L/R", "M/S"});
    for (juce::ComboBox* combo : {&bandSelect, &typeSelect, &phaseSelect, &slopeSelect, &stereoSelect})
        addAndMakeVisible(*combo);

    for (juce::Slider* slider : {&freq, &gain, &q, &output})
    {
        prepareRotary(*slider);
        addAndMakeVisible(*slider);
    }
    addAndMakeVisible(enabledButton);
    enabledButton.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgreen);

    outputAtt = std::make_unique<SlAtt>(processor.state, "outputGain", output);
    phaseAtt = std::make_unique<CbAtt>(processor.state, "phaseMode", phaseSelect);
    slopeAtt = std::make_unique<CbAtt>(processor.state, "slope", slopeSelect);
    stereoAtt = std::make_unique<CbAtt>(processor.state, "stereoMode", stereoSelect);

    // Install the callback before selecting a band, but suppress notification for
    // the initial selection to avoid reentrant attachment construction.
    bandSelect.onChange = [this] { switchBand(bandSelect.getSelectedId() - 1); };
    selectedBand = juce::jlimit(0, 7, (int) processor.state.state.getProperty("selectedBand", 0));
    bandSelect.setSelectedId(selectedBand + 1, juce::dontSendNotification);
    switchBand(selectedBand);

    // A modest, host-friendly window size, with no resize listener or timer.
    setSize(720, 410);
    SkellyTrace::checkpoint("stable-editor: constructor EXIT");
}

SkellyEQStableEditor::~SkellyEQStableEditor()
{
    SkellyTrace::checkpoint("stable-editor: destructor ENTER");
    bandSelect.onChange = nullptr;
    freqAtt.reset();
    gainAtt.reset();
    qAtt.reset();
    outputAtt.reset();
    typeAtt.reset();
    phaseAtt.reset();
    slopeAtt.reset();
    stereoAtt.reset();
    enabledAtt.reset();
    SkellyTrace::checkpoint("stable-editor: destructor EXIT");
}

void SkellyEQStableEditor::switchBand(int index)
{
    if (! juce::isPositiveAndBelow(index, 8))
        return;
    freqAtt.reset();
    gainAtt.reset();
    qAtt.reset();
    typeAtt.reset();
    enabledAtt.reset();

    selectedBand = index;
    processor.state.state.setProperty("selectedBand", selectedBand, nullptr);
    freqAtt = std::make_unique<SlAtt>(processor.state, stableBandParameter(index, "freq"), freq);
    gainAtt = std::make_unique<SlAtt>(processor.state, stableBandParameter(index, "gain"), gain);
    qAtt = std::make_unique<SlAtt>(processor.state, stableBandParameter(index, "q"), q);
    typeAtt = std::make_unique<CbAtt>(processor.state, stableBandParameter(index, "type"), typeSelect);
    enabledAtt = std::make_unique<BtAtt>(processor.state, stableBandParameter(index, "enabled"), enabledButton);
}

void SkellyEQStableEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff070e09));
    g.setColour(juce::Colour(0xff24582b));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(8.0f), 8.0f, 1.0f);
    g.drawLine(20.0f, 76.0f, 700.0f, 76.0f, 1.0f);
    g.drawLine(20.0f, 284.0f, 700.0f, 284.0f, 1.0f);
}

void SkellyEQStableEditor::resized()
{
    title.setBounds(25, 18, 280, 42);
    buildLabel.setBounds(305, 23, 392, 34);
    bandLabel.setBounds(22, 92, 110, 18);
    bandSelect.setBounds(22, 114, 116, 28);
    enabledButton.setBounds(160, 116, 140, 28);
    typeLabel.setBounds(319, 92, 155, 18);
    typeSelect.setBounds(319, 114, 156, 28);
    phaseLabel.setBounds(502, 92, 194, 18);
    phaseSelect.setBounds(502, 114, 194, 28);

    constexpr int start = 57, width = 120, space = 167;
    juce::Slider* knobs[] = {&freq, &gain, &q, &output};
    juce::Label* labels[] = {&freqLabel, &gainLabel, &qLabel, &outputLabel};
    for (int i = 0; i < 4; ++i)
    {
        const auto x = start + i * space;
        labels[i]->setBounds(x, 162, width, 20);
        knobs[i]->setBounds(x, 184, width, 97);
    }
    slopeLabel.setBounds(90, 316, 150, 18);
    slopeSelect.setBounds(90, 340, 150, 28);
    stereoLabel.setBounds(400, 316, 150, 18);
    stereoSelect.setBounds(400, 340, 150, 28);
}
