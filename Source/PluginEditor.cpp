#include "PluginEditor.h"
#include "Theme.h"
#include <BinaryData.h>
static juce::String selectedId(int b,const char* what){return "b"+juce::String(b+1)+"_"+what;}
SkellyEQAudioProcessorEditor::SkellyEQAudioProcessorEditor(SkellyEQAudioProcessor& p):AudioProcessorEditor(&p),processor(p),graph(p,selected),meter(p){
 setLookAndFeel(&look);setResizable(true,true);getConstrainer()->setFixedAspectRatio(Theme::width/Theme::height);setResizeLimits(892,441,2676,1323);
 frameImage=juce::ImageFileFormat::loadFrom(BinaryData::frame_png,BinaryData::frame_pngSize);
 skullImage=juce::ImageFileFormat::loadFrom(BinaryData::skull_png,BinaryData::skull_pngSize);
 graphImage=juce::ImageFileFormat::loadFrom(BinaryData::graph_bg_png,BinaryData::graph_bg_pngSize);
 selected=juce::jlimit(0,7,(int)processor.state.state.getProperty("selectedBand",0));
 addAndMakeVisible(graph);addAndMakeVisible(meter);addAndMakeVisible(header);graph.selectionChanged=[this]{selectBand(selected);};
 auto get=[this](const char* key){return (int)processor.state.getRawParameterValue(key)->load();};
 phase=std::make_unique<SegmentedControl>(juce::StringArray{"LINEAR","MIN PHASE","ZERO LATENCY"},[this,get]{return get("phaseMode");},[this](int v){parameterSet("phaseMode",(float)v);});addAndMakeVisible(*phase);
 slope=std::make_unique<SegmentedControl>(juce::StringArray{"12","24","36","48"},[this,get]{return get("slope");},[this](int v){parameterSet("slope",(float)v);});addAndMakeVisible(*slope);
 const char* names[]={"LOW CUT","PEAK","HIGH CUT","NOTCH","BAND"};int ids[]={3,0,4,5,6};for(int i=0;i<5;++i){types[i]=std::make_unique<IconToggle>(names[i]);auto t=ids[i];types[i]->onClick=[this,t]{parameterSet(selectedId(selected,"type"),(float)t);};addAndMakeVisible(*types[i]);}
 for(auto* knob:{&freqKnob,&gainKnob,&qKnob,&amountKnob})addAndMakeVisible(*knob);
 amountKnob.setRange(0,100,.1);amountAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,"analyzeAmount",amountKnob);bindKnobs();
 auto init=[this](juce::TextButton& b,std::function<void()> fn){b.onClick=std::move(fn);addAndMakeVisible(b);b.setMouseCursor(juce::MouseCursor::PointingHandCursor);};
 init(licensed,[this]{juce::PopupMenu m;m.addItem(1,"Skelly EQ 1.0 — Skelly Audio");m.addItem(2,"Original JUCE EQ project");m.showMenuAsync(juce::PopupMenu::Options());});
 init(options,[this]{juce::PopupMenu m;m.addItem(1,"Graph: ±6 dB");m.addItem(2,"Graph: ±12 dB");m.addItem(3,"Graph: ±18 dB");m.addItem(4,"Graph: ±30 dB");juce::Component::SafePointer<SkellyEQAudioProcessorEditor> safe(this);m.showMenuAsync(juce::PopupMenu::Options(),[safe](int v){if(safe&&v>=1&&v<=4)safe->parameterSet("graphRange",(float)(v-1));});});
 init(ab,[this]{processor.storeSnapshot(abSlot);abSlot=1-abSlot;processor.loadSnapshot(abSlot);ab.setButtonText(abSlot?"B | A":"A | B");});
 init(nextPreset,[this]{juce::PopupMenu m;m.addItem(1,"Flat");m.addItem(2,"Original contour");juce::Component::SafePointer<SkellyEQAudioProcessorEditor> safe(this);m.showMenuAsync(juce::PopupMenu::Options(),[safe](int v){if(!safe||!v)return;const float gains[]={1,-2,-4,-3,-11,-2,7,-1.5f};for(int i=0;i<8;++i)safe->parameterSet(selectedId(i,"gain"),v==1?0.f:gains[i]);});});
 init(previous,[this]{selectBand((selected+7)%8);});init(next,[this]{selectBand((selected+1)%8);});
 init(analyze,[this]{int v=((int)processor.state.getRawParameterValue("analyzeMode")->load()+1)%4;parameterSet("analyzeMode",(float)v);});
 init(stereo,[this]{parameterSet("stereoMode",0);});init(ms,[this]{parameterSet("stereoMode",1);});
 setSize((int)Theme::width,(int)Theme::height);startTimerHz(25);
}
SkellyEQAudioProcessorEditor::~SkellyEQAudioProcessorEditor(){setLookAndFeel(nullptr);}
void SkellyEQAudioProcessorEditor::parameterSet(const juce::String& id,float v){if(auto* p=processor.state.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));p->endChangeGesture();}}
void SkellyEQAudioProcessorEditor::selectBand(int index){selected=juce::jlimit(0,7,index);processor.state.state.setProperty("selectedBand",selected,nullptr);bindKnobs();graph.repaint();}
void SkellyEQAudioProcessorEditor::bindKnobs(){freqAttachment.reset();gainAttachment.reset();qAttachment.reset();freqKnob.setRange(20,20000,.01);gainKnob.setRange(-18,18,.01);qKnob.setRange(.1,18,.01);freqAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,selectedId(selected,"freq"),freqKnob);gainAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,selectedId(selected,"gain"),gainKnob);qAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,selectedId(selected,"q"),qKnob);}
void SkellyEQAudioProcessorEditor::paint(juce::Graphics& g){auto bounds=getLocalBounds().toFloat();g.fillAll(Theme::bg);auto sx=bounds.getWidth()/Theme::width,sy=bounds.getHeight()/Theme::height;g.addTransform(juce::AffineTransform::scale(sx,sy));
 if(graphImage.isValid()){g.setOpacity(.40f);g.drawImage(graphImage,Theme::graph,juce::RectanglePlacement::stretchToFit);g.setOpacity(1);}if(frameImage.isValid())g.drawImage(frameImage,0,0,Theme::width,Theme::height,0,0,frameImage.getWidth(),frameImage.getHeight());if(skullImage.isValid())g.drawImage(skullImage,785,0,206,182,0,0,skullImage.getWidth(),skullImage.getHeight());
 Theme::panelBox(g,{1546,120,179,633});Theme::label(g,"FREQ",{630,826,105,23},18,true,Theme::green);Theme::label(g,"GAIN",{786,826,105,23},18,true,Theme::green);Theme::label(g,"Q",{951,826,105,23},18,true,Theme::green);
 Theme::label(g,"ANALYZE",{1535,613,86,20},13,true,Theme::green);Theme::label(g,"AMOUNT",{1620,613,91,20},13,true,Theme::green);Theme::label(g,juce::String((int)amountKnob.getValue())+"%",{1626,720,83,20},15,true,Theme::green);
 for(int db=-18;db<=18;db+=6){juce::String s=db>0?"+"+juce::String(db):juce::String(db);Theme::label(g,s,{29,Theme::graph.getY()+(18-db)/36.f*Theme::graph.getHeight()-10,51,20},15,true,Theme::glow);}
 const int freq[]={20,50,100,200,500,1000,2000,5000,10000,20000};for(int f:freq){float x=Theme::graph.getX()+std::log((float)f/20.f)/std::log(1000.f)*Theme::graph.getWidth();Theme::label(g,f>=1000?juce::String(f/1000)+"K":juce::String(f),{x-25,691,50,27},16,true,Theme::glow);}
}
void SkellyEQAudioProcessorEditor::resized(){auto sx=getWidth()/Theme::width,sy=getHeight()/Theme::height;auto place=[sx,sy](juce::Component& c,float x,float y,float w,float h){c.setBounds(juce::Rectangle<float>{x*sx,y*sy,w*sx,h*sy}.toNearestInt());};
 place(header,57,20,480,80);place(graph,83,137,1450,545);place(meter,1557,139,145,303);place(*phase,1120,522,280,38);place(*slope,1160,566,240,39);
 place(licensed,1360,55,113,40);place(options,1487,55,98,40);place(ab,1596,55,71,40);place(nextPreset,1683,55,45,40);
 place(*types[0],1546,455,55,74);place(*types[1],1603,455,55,74);place(*types[2],1660,455,55,74);place(*types[3],1550,533,79,42);place(*types[4],1634,533,79,42);
 place(stereo,1550,581,78,35);place(ms,1633,581,79,35);place(analyze,1537,718,85,31);
 place(amountKnob,1635,640,70,70);place(freqKnob,638,741,102,90);place(gainKnob,792,741,102,90);place(qKnob,956,741,102,90);place(previous,532,763,44,44);place(next,1112,763,44,44);
}
void SkellyEQAudioProcessorEditor::timerCallback(){int m=(int)processor.state.getRawParameterValue("analyzeMode")->load();const char* modes[]={"OFF","PRE","POST","ON"};analyze.setButtonText(juce::String("ANALYZE: ")+modes[juce::jlimit(0,3,m)]);phase->repaint();slope->repaint();auto type=(int)processor.state.getRawParameterValue(selectedId(selected,"type"))->load();const int indices[]={3,0,4,5,6};for(int i=0;i<5;++i)types[i]->setToggleState(type==indices[i],juce::dontSendNotification);repaint();}
