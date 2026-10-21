#pragma once
#include <JuceHeader.h>
#include "dsp/BiquadCascade.h"
#include "dsp/Analyzer.h"
class SkellyEQAudioProcessor final:public juce::AudioProcessor {
public:
 SkellyEQAudioProcessor();
 ~SkellyEQAudioProcessor() override;
 void prepareToPlay(double,int) override;void releaseResources() override {}
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
 juce::AudioProcessorEditor* createEditor() override; bool hasEditor() const override{return true;}
 const juce::String getName() const override{return "Skelly EQ";}
 bool acceptsMidi() const override{return false;}bool producesMidi() const override{return false;}
 bool isMidiEffect() const override{return false;}double getTailLengthSeconds() const override{return 0;}
 int getNumPrograms() override{return 1;}int getCurrentProgram() override{return 0;}void setCurrentProgram(int) override{}
 const juce::String getProgramName(int) override{return {}; }void changeProgramName(int,const juce::String&) override{}
 void getStateInformation(juce::MemoryBlock&) override;void setStateInformation(const void*,int) override;
 static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
 juce::AudioProcessorValueTreeState state{*this,nullptr,"PARAMETERS",makeParameters()};
 std::array<SkellyDSP::BandConfig,8> bands() const;
 std::atomic<float> peaks[2]{{0},{0}}, rms[2]{{0},{0}};
 SkellyDSP::Analyzer analyzer;
 double sampleRateHz() const {return getSampleRate()>0?getSampleRate():44100.0;}
 void storeSnapshot(int slot);void loadSnapshot(int slot);
private:
 struct Stage{SkellyDSP::Biquad filt[2][4];};
 Stage filters[8];
 SkellyDSP::BandConfig previous[8]{};
 int lastSlope=-1,lastMode=-1;
 juce::SmoothedValue<float> outputSmooth;
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellyEQAudioProcessor)
};
