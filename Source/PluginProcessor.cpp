#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace SkellyDSP;
static juce::String id(int i,const char* n){return "b"+juce::String(i+1)+"_"+n;}
SkellyEQAudioProcessor::SkellyEQAudioProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)){}
juce::AudioProcessorValueTreeState::ParameterLayout SkellyEQAudioProcessor::makeParameters(){
 std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
 auto f=[&](juce::String key,juce::String name,float lo,float hi,float def,float skew=1.f){juce::NormalisableRange<float> r(lo,hi);r.setSkewForCentre(skew);p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(key,1),name,r,def));};
 auto c=[&](juce::String key,juce::String name,juce::StringArray values,int def){p.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(key,1),name,values,def));};
 auto b=[&](juce::String key,juce::String name,bool def){p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(key,1),name,def));};
 const double freqs[]={25,65,130,300,900,7000,10000,19000};const double gains[]={1,-2,-4,-3,-11,-2,7,-1.5};
 for(int i=0;i<8;++i){auto n=juce::String(i+1);b(id(i,"enabled"),"Band "+n+" Enable",true);c(id(i,"type"),"Band "+n+" Type",{"Peak","Low Shelf","High Shelf","Low Cut","High Cut","Notch","Band Pass","Tilt"},0);f(id(i,"freq"),"Band "+n+" Frequency",20,20000,(float)freqs[i],1000);f(id(i,"gain"),"Band "+n+" Gain",-18,18,(float)gains[i]);f(id(i,"q"),"Band "+n+" Q",.1f,18,1.0f,1);c(id(i,"channel"),"Band "+n+" Channel",{"Stereo","Left","Right","Mid","Side"},0);b(id(i,"solo"),"Band "+n+" Solo",false);}
 c("phaseMode","Phase Mode",{"Linear","Min Phase","Zero Latency"},1);c("slope","Cut Slope",{"12","24","36","48"},3);c("stereoMode","Stereo Mode",{"L/R","M/S"},0);f("outputGain","Output Gain",-18,18,0);c("analyzeMode","Analyzer Mode",{"Off","Pre","Post","On"},3);f("analyzeAmount","Analyzer Amount",0,100,100);c("graphRange","Graph Range",{"6","12","18","30"},2);
 return {p.begin(),p.end()};
}
std::array<BandConfig,8> SkellyEQAudioProcessor::bands() const {std::array<BandConfig,8> a;for(int i=0;i<8;++i){auto val=[&](const char* s){return state.getRawParameterValue(id(i,s))->load();};a[i]={val("enabled")>.5f,val("solo")>.5f,(int)val("type"),(int)val("channel"),val("freq"),val("gain"),val("q")};}return a;}
void SkellyEQAudioProcessor::prepareToPlay(double sr,int){for(auto& stage:filters)for(auto& channel:stage.filt)for(auto& filter:channel)filter.reset();lastSlope=-1;outputSmooth.reset(sr,.02);outputSmooth.setCurrentAndTargetValue(1);setLatencySamples(0);}
void SkellyEQAudioProcessor::processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer&){juce::ScopedNoDenormals noDenormals;auto n=audio.getNumSamples();if(audio.getNumChannels()<2)return;auto all=bands();int slope=1+(int)state.getRawParameterValue("slope")->load(),mode=(int)state.getRawParameterValue("stereoMode")->load();
 bool anySolo=false;for(auto& b:all)anySolo|=b.solo;
 // Coefficients update at block boundaries; states persist between blocks.
 for(int i=0;i<8;++i){const auto& band=all[i];if(previous[i].enabled!=band.enabled || previous[i].type!=band.type || previous[i].freq!=band.freq || previous[i].gain!=band.gain || previous[i].q!=band.q || slope!=lastSlope){int ty=internalType(band.type);int stages=(ty==3||ty==4)?slope:1;for(int channel=0;channel<2;++channel)for(int k=0;k<4;++k)filters[i].filt[channel][k].c=k<stages?design(ty,band.freq,band.gain,stages>1?cutoffQ(k,stages):band.q,sampleRateHz()):Coeff{};previous[i]=band;}}
 lastSlope=slope;lastMode=mode;
 float target=juce::Decibels::decibelsToGain(state.getRawParameterValue("outputGain")->load());outputSmooth.setTargetValue(target);
 int analysis=(int)state.getRawParameterValue("analyzeMode")->load();float peak[2]{0,0},sum[2]{0,0};auto* left=audio.getWritePointer(0);auto* right=audio.getWritePointer(1);
 for(int j=0;j<n;++j){double L=left[j],R=right[j];if(analysis==1)analyzer.push((float)((L+R)*.5));
 double v[2]{L,R};if(mode==1){v[0]=(L+R)*.7071067811865476;v[1]=(L-R)*.7071067811865476;}
 for(int i=0;i<8;++i){const auto& b=all[i];if(!b.enabled||(anySolo&&!b.solo))continue;int t=internalType(b.type),sections=(t==3||t==4)?slope:1;
 for(int ch=0;ch<2;++ch){bool route=mode==0?(b.channel==0||(b.channel==1&&ch==0)||(b.channel==2&&ch==1)):(b.channel==0||(b.channel==3&&ch==0)||(b.channel==4&&ch==1));if(!route)continue;for(int k=0;k<sections;++k)v[ch]=filters[i].filt[ch][k].tick(v[ch]);}}
 if(mode==1){L=(v[0]+v[1])*.7071067811865476;R=(v[0]-v[1])*.7071067811865476;}else {L=v[0];R=v[1];}
 double gain=outputSmooth.getNextValue();left[j]=(float)(L*gain);right[j]=(float)(R*gain);peak[0]=juce::jmax(peak[0],std::abs(left[j]));peak[1]=juce::jmax(peak[1],std::abs(right[j]));sum[0]+=left[j]*left[j];sum[1]+=right[j]*right[j];if(analysis>=2)analyzer.push((left[j]+right[j])*.5f);}
 for(int ch=0;ch<2;++ch){peaks[ch].store(peak[ch]);rms[ch].store(std::sqrt(sum[ch]/juce::jmax(1,n)));}}
void SkellyEQAudioProcessor::getStateInformation(juce::MemoryBlock& dest){auto xml=state.copyState().createXml();copyXmlToBinary(*xml,dest);}
void SkellyEQAudioProcessor::setStateInformation(const void* data,int bytes){if(auto xml=getXmlFromBinary(data,bytes))if(xml->hasTagName(state.state.getType()))state.replaceState(juce::ValueTree::fromXml(*xml));}
void SkellyEQAudioProcessor::storeSnapshot(int slot){auto snap=state.copyState();snap.setProperty("snapshotSlot",slot,nullptr);auto key=slot==0?"snapshotA":"snapshotB";state.state.removeChild(state.state.getChildWithName(key),nullptr);juce::ValueTree saved(key);for(int i=0;i<snap.getNumChildren();++i)saved.addChild(snap.getChild(i).createCopy(),-1,nullptr);state.state.addChild(saved,-1,nullptr);}
void SkellyEQAudioProcessor::loadSnapshot(int slot){auto s=state.state.getChildWithName(slot==0?"snapshotA":"snapshotB");if(!s.isValid())return;for(int i=0;i<s.getNumChildren();++i){auto p=s.getChild(i);auto idName=p.getProperty("id").toString();if(auto* target=state.getParameter(idName)){auto val=(float)p.getProperty("value");target->setValueNotifyingHost(target->convertTo0to1(val));}}}
juce::AudioProcessorEditor* SkellyEQAudioProcessor::createEditor(){return new SkellyEQAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new SkellyEQAudioProcessor();}
