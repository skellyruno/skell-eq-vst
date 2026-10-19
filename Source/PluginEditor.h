#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/EqGraph.h"
#include "ui/BandKnob.h"
#include "ui/MeterPanel.h"
#include "ui/SegmentedControl.h"
#include "ui/IconToggle.h"
#include "ui/HeaderBar.h"
#include "ui/LookAndFeel.h"
class SkellyEQAudioProcessorEditor final:public juce::AudioProcessorEditor,private juce::Timer {
public:
 explicit SkellyEQAudioProcessorEditor(SkellyEQAudioProcessor&);~SkellyEQAudioProcessorEditor() override;
 void paint(juce::Graphics&)override;void resized()override;
private:
 void timerCallback()override;
 void selectBand(int);void parameterSet(const juce::String&,float);
 void bindKnobs();
 SkellyEQAudioProcessor& processor;SkellyLookAndFeel look;
 int selected=0,abSlot=0;
 EqGraph graph;MeterPanel meter;HeaderBar header;
 BandKnob freqKnob,gainKnob,qKnob,amountKnob;
 std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttachment,gainAttachment,qAttachment,amountAttachment;
 std::unique_ptr<SegmentedControl> phase,slope;
 juce::TextButton licensed{"LICENSED"},options{"OPTION"},ab{"A | B"},nextPreset{">"},previous{"<"},next{">"}, analyze{"ANALYZE: ON"},stereo{"L/R"},ms{"M/S"};
 std::array<std::unique_ptr<IconToggle>,5> types;
 juce::Image frameImage,skullImage,graphImage;
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellyEQAudioProcessorEditor)
};
