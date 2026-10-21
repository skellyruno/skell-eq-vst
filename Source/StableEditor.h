#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// Intentionally plain editor for host crash isolation. No timers, image decoding,
// async menus, custom graphics FFT, or borrowed audio-thread buffers.
class SkellyEQStableEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SkellyEQStableEditor(SkellyEQAudioProcessor&);
    ~SkellyEQStableEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SlAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CbAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BtAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void switchBand(int index);
    static void prepareRotary(juce::Slider& slider);
    static void prepareLabel(juce::Label& label, const juce::String& title);
    static void prepareChoice(juce::ComboBox& combo, const juce::StringArray& options);

    SkellyEQAudioProcessor& processor;
    int selectedBand = 0;

    juce::ComboBox bandSelect, typeSelect, phaseSelect, slopeSelect, stereoSelect;
    juce::ToggleButton enabledButton {"Band enabled"};
    juce::Slider freq, gain, q, output;
    juce::Label title, buildLabel, freqLabel, gainLabel, qLabel, outputLabel,
                typeLabel, phaseLabel, slopeLabel, stereoLabel, bandLabel;

    std::unique_ptr<SlAtt> freqAtt, gainAtt, qAtt, outputAtt;
    std::unique_ptr<CbAtt> typeAtt, phaseAtt, slopeAtt, stereoAtt;
    std::unique_ptr<BtAtt> enabledAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellyEQStableEditor)
};
