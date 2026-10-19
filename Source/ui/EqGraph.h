#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../Theme.h"
class EqGraph:public juce::Component,public juce::SettableTooltipClient,private juce::Timer {
public:
 EqGraph(SkellyEQAudioProcessor& p,int& selection):processor(p),selected(selection){setMouseCursor(juce::MouseCursor::CrosshairCursor);startTimerHz(40);}
 void paint(juce::Graphics&) override;void mouseDown(const juce::MouseEvent&) override;void mouseDrag(const juce::MouseEvent&) override;void mouseUp(const juce::MouseEvent&) override;
 void mouseDoubleClick(const juce::MouseEvent&) override;void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override;
 void mouseMove(const juce::MouseEvent&) override;
 std::function<void()> selectionChanged;
private:
 void timerCallback() override;
 float xFromFreq(float f) const{return (float)(std::log(f/20.f)/std::log(1000.f)*getWidth());}
 float freqFromX(float x) const{return 20.f*std::pow(1000.f,juce::jlimit(0.f,1.f,x/getWidth()));}
 float yFromGain(float g) const{return (18.f-g)/36.f*getHeight();}
 float gainFromY(float y) const{return juce::jlimit(-18.f,18.f,18.f-36.f*y/getHeight());}
 int hit(juce::Point<float>) const;
 void edit(int band,const char*,float);
 SkellyEQAudioProcessor& processor;int& selected;int dragging=-1;float startFreq=0,startGain=0,startQ=0;juce::Point<float> startPoint;
 juce::dsp::FFT fft{12};juce::dsp::WindowingFunction<float> window{4096,juce::dsp::WindowingFunction<float>::hann};
 std::array<float,4096> time{};std::array<float,8192> spectrumData{};std::array<float,256> spectrum{};int filled=0;
};
