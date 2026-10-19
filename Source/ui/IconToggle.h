#pragma once
#include <JuceHeader.h>
#include "../Theme.h"
class IconToggle:public juce::TextButton {public:explicit IconToggle(const juce::String& s):juce::TextButton(s){setColour(buttonColourId,Theme::panel);setColour(buttonOnColourId,Theme::dim);setColour(textColourOffId,Theme::green);setColour(textColourOnId,Theme::text);}};
