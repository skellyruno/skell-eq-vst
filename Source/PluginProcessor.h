#pragma once
#include <JuceHeader.h>
#include "dsp/BiquadCascade.h"
#include "dsp/Analyzer.h"
#include "dsp/LinearPhaseFIR.h"
#include <memory>
class SkellyEQAudioProcessor final:public juce::AudioProcessor, private juce::AsyncUpdater, private juce::AudioProcessorValueTreeState::Listener {
public:
 SkellyEQAudioProcessor();
 ~SkellyEQAudioProcessor() override;
 void prepareToPlay(double,int) override; void releaseResources() override;
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
 juce::AudioProcessorEditor* createEditor() override; bool hasEditor() const override{return true;}
 const juce::String getName() const override{return "Skelly EQ";}
 bool acceptsMidi() const override{return false;}bool producesMidi() const override{return false;}
 bool isMidiEffect() const override{return false;}
 double getTailLengthSeconds() const override {
    return observedPhase.load(std::memory_order_acquire)==0
        ? double(SkellyDSP::StereoLinearFIR::impulseSize)/sampleRateHz() : 0.0;
 }
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
 class DesignerThread;
 struct Stage;
 void handleAsyncUpdate() override;
 void parameterChanged(const juce::String& parameterID, float newValue) override;
 void processMinimumSample(double inL, double inR, int mode,
     struct Stage (&banks)[8], struct Stage (&retired)[8],
     SkellyDSP::WetDryRamp (&topology)[8],
     SkellyDSP::WetDryRamp (&wet)[8][2], const int* stages, const int* retiredCounts,
     double& outL, double& outR) noexcept;
 struct Stage{SkellyDSP::Biquad filt[2][4];};
 Stage filters[8];
 // Retired bank continues processing briefly while filter topology changes.
 // This avoids cutting a resonant filter state off on the audio thread.
 Stage retiredFilters[8];
 Stage switchFilters[8];
 Stage switchRetiredFilters[8];
 SkellyDSP::WetDryRamp switchTopology[8];
 SkellyDSP::WetDryRamp switchBandMix[8][2];
 int switchStageCounts[8]{};
 int switchRetiredStages[8]{};
 SkellyDSP::WetDryRamp stereoFade;
 int activeStereoMode=-1, previousStereoMode=0;
 bool stereoTransition=false;
 SkellyDSP::WetDryRamp topologyMix[8];
 int retiredStages[8]{};
 // Per-lane mix smoothing keeps bypass, solo and routing changes click-resistant.
 SkellyDSP::WetDryRamp bandMix[8][2];
 SkellyDSP::BandConfig previous[8]{};
 int lastSlope=-1,lastMode=-1;
 juce::SmoothedValue<float> outputSmooth;
 std::unique_ptr<SkellyDSP::StereoLinearFIR> linearEngine;
 std::unique_ptr<DesignerThread> designer;
 std::atomic<int> observedPhase{1};
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellyEQAudioProcessor)
};
