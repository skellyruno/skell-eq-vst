#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
class MeterPanel:public juce::Component,private juce::Timer {
public:
 explicit MeterPanel(SkellyEQAudioProcessor& p):processor(p){startTimerHz(30);}void paint(juce::Graphics&)override;
private:void timerCallback()override{for(int i=0;i<2;++i){display[i]=juce::jmax(processor.peaks[i].load(),display[i]*.88f);rms[i]=juce::jmax(processor.rms[i].load(),rms[i]*.88f);}repaint();}SkellyEQAudioProcessor& processor;float display[2]{0,0},rms[2]{0,0};};
