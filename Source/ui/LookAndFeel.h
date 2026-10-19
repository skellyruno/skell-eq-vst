#pragma once
#include <JuceHeader.h>
#include "../Theme.h"
class SkellyLookAndFeel:public juce::LookAndFeel_V4 {public:SkellyLookAndFeel(){setColour(juce::TextButton::buttonColourId,Theme::panel);setColour(juce::TextButton::textColourOffId,Theme::green);setColour(juce::PopupMenu::backgroundColourId,Theme::panel);setColour(juce::PopupMenu::textColourId,Theme::text);setColour(juce::PopupMenu::highlightedBackgroundColourId,Theme::dim);}};
