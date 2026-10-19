#pragma once
#include <JuceHeader.h>
#include "../Theme.h"
class HeaderBar:public juce::Component{public:void paint(juce::Graphics&g)override{g.setColour(Theme::green);g.setFont(Theme::font(37,true));g.drawText("SKELLY EQ",18,4,370,47,juce::Justification::left);g.setFont(Theme::font(14,true));g.drawText("BONE-DEEP PARAMETRIC",21,48,350,19,juce::Justification::left);}};
