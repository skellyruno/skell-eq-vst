#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <memory>

// V1.5 staged visual restoration: no FFT, background worker, async PopupMenu,
// or borrowed raw pointers. The V1.4 stable editor remains a build option.
class SkellyEQRestoredEditor final : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit SkellyEQRestoredEditor(SkellyEQAudioProcessor&);
    ~SkellyEQRestoredEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class NeonLook final : public juce::LookAndFeel_V4
    {
    public:
        NeonLook();
        void drawRotarySlider(juce::Graphics&, int, int, int, int, float,
                              float, float, juce::Slider&) override;
    } look;

    SkellyEQAudioProcessor& processor;
    int selectedBand = 0;

    class ResponseGraph final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        ResponseGraph(SkellyEQAudioProcessor&, int&);
        ~ResponseGraph() override { endDragParam(); }
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseMove(const juce::MouseEvent&) override;
        std::function<void(int)> onSelection;
        juce::Image graphTexture;

    private:
        juce::RangedAudioParameter* param(int, const char*) const;
        void setParam(int, const char*, float);
        float xForFrequency(float) const;
        float frequencyForX(float) const;
        float yForGain(float) const;
        float gainForY(float) const;
        int findBand(juce::Point<float>) const;
        void beginDragParam(juce::RangedAudioParameter*);
        void endDragParam();

        SkellyEQAudioProcessor& processor;
        int& selected;
        int dragged = -1;
        float originalFrequency = 0, originalGain = 0, originalQ = 0;
        juce::Point<float> origin;
        juce::RangedAudioParameter* gestureParam = nullptr;
        juce::Path cachedResponse;
        std::array<SkellyDSP::BandConfig, 8> cachedBands{};
        bool curveValid = false;
        int cachedSlope = -1, cachedRange = -1, cachedWidth = -1, cachedHeight = -1;
        double cachedRate = -1.0;
    } graph;

    void timerCallback() override;
    void selectBand(int);
    void rebindBand();
    void setGlobal(const juce::String&, float);
    void setBandType(int);
    void drawMeters(juce::Graphics&);
    static juce::String bandId(int, const char*);

    juce::Image skin;
    juce::ComboBox bandSelect, typeSelect, phaseSelect, slopeSelect, modeSelect;
    juce::ToggleButton enabled {"ACTIVE"};
    juce::Slider frequency, gain, q, output;
    juce::TextButton options {"OPTION"}, about {"SKELLY EQ"}, previous {"<"}, next {">"};
    juce::TextButton typeButtons[5];
    juce::Label buildTag;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> freqAttachment, gainAttachment, qAttachment, outputAttachment;
    std::unique_ptr<ComboAttachment> typeAttachment, phaseAttachment, slopeAttachment, modeAttachment;
    std::unique_ptr<ButtonAttachment> enabledAttachment;
    float meterPeak[2]{}, meterRms[2]{};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellyEQRestoredEditor)
};
