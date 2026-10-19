#pragma once
#include <JuceHeader.h>
#include "../Theme.h"
class BandKnob:public juce::Slider {
public:
 BandKnob(){setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);setMouseCursor(juce::MouseCursor::PointingHandCursor);}
 void paint(juce::Graphics& g) override {auto b=getLocalBounds().toFloat();auto d=juce::jmin(b.getWidth(),b.getHeight())*.78f;auto r=juce::Rectangle<float>(d,d).withCentre(b.getCentre());g.setColour(Theme::green.withAlpha(.15f));g.fillEllipse(r.expanded(6));g.setColour(Theme::panel);g.fillEllipse(r);g.setColour(Theme::dim);g.drawEllipse(r,2);
 auto a=juce::MathConstants<float>::pi*1.25f+(float)valueToProportionOfLength(getValue())*juce::MathConstants<float>::pi*1.5f;juce::Path arc;arc.addCentredArc(r.getCentreX(),r.getCentreY(),d*.5f+3,d*.5f+3,0,juce::MathConstants<float>::pi*1.25f,a,true);g.setColour(Theme::green);g.strokePath(arc,juce::PathStrokeType(3.5f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));juce::Path pointer;pointer.startNewSubPath(r.getCentre());pointer.lineTo(r.getCentre()+juce::Point<float>(std::sin(a),-std::cos(a))*d*.32f);g.setColour(Theme::glow);g.strokePath(pointer,juce::PathStrokeType(3.f));}
};
