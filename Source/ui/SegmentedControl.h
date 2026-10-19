#pragma once
#include <JuceHeader.h>
#include "../Theme.h"
class SegmentedControl:public juce::Component {
public:
 SegmentedControl(juce::StringArray labels,std::function<int()> get,std::function<void(int)> set):names(std::move(labels)),getter(std::move(get)),setter(std::move(set)){setMouseCursor(juce::MouseCursor::PointingHandCursor);}
 void paint(juce::Graphics& g) override {Theme::panelBox(g,getLocalBounds().toFloat());auto n=names.size();auto cw=getWidth()/(float)n;for(int i=0;i<n;++i){auto r=juce::Rectangle<float>(i*cw+2,2,cw-4,(float)getHeight()-4);if(i==getter()){g.setColour(Theme::green);g.fillRoundedRectangle(r,4);}Theme::label(g,names[i],r,juce::jmin(13.f,cw*.15f),true,i==getter()?Theme::bg:Theme::text);}}
 void mouseUp(const juce::MouseEvent& e)override{setter(juce::jlimit(0,names.size()-1,(int)(e.position.x/getWidth()*names.size())));repaint();}
private:juce::StringArray names;std::function<int()>getter;std::function<void(int)>setter;};
