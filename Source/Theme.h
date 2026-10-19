#pragma once
#include <JuceHeader.h>
namespace Theme {
inline const juce::Colour bg {0xff050a06}, panel {0xff0b170c}, green {0xff39ff3a}, glow {0xff7dff7a}, dim {0xff1f7a22}, amber {0xffffc21a}, text {0xffd8d8d8};
constexpr float width=1784.f, height=882.f;
inline const juce::Rectangle<float> graph{83,137,1450,545}, right{1545,123,171,635}, bottom{480,731,720,135};
inline juce::Font font(float h, bool bold=false) { return juce::Font(juce::FontOptions(h).withStyle(bold ? "Bold" : "Regular")); }
inline void panelBox(juce::Graphics& g,juce::Rectangle<float> r,float radius=6) {g.setColour(panel);g.fillRoundedRectangle(r,radius);g.setColour(dim.withAlpha(.85f));g.drawRoundedRectangle(r,radius,1.5f);}
inline void label(juce::Graphics& g,const juce::String& s,juce::Rectangle<float> r,float size=15,bool strong=false,juce::Colour c=text) {g.setColour(c);g.setFont(font(size,strong));g.drawFittedText(s,r.toNearestInt(),juce::Justification::centred,1);}
}
