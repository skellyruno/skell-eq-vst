#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "StableEditor.h"
#include "RestoredEditor.h"
#include "CrashTrace.h"
#include <cmath>
using namespace SkellyDSP;
static juce::String id(int i,const char* n){return "b"+juce::String(i+1)+"_"+n;}
SkellyEQAudioProcessor::SkellyEQAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    SkellyTrace::checkpoint("processor: constructor EXIT");
}
SkellyEQAudioProcessor::~SkellyEQAudioProcessor()
{
    SkellyTrace::checkpoint("processor: destructor ENTER");
}
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
void SkellyEQAudioProcessor::prepareToPlay(double sr,int)
{
    SkellyTrace::checkpoint("processor: prepareToPlay ENTER");
    for (auto& stage : filters)
        for (auto& channel : stage.filt)
            for (auto& filter : channel)
            {
                filter.reset();
                filter.setCoefficients(Coeff{}, 0, true);
            }
    lastSlope = -1;
    lastMode = -1;
    const double safeRate = (std::isfinite(sr) && sr >= 8000.0) ? sr : 44100.0;
    outputSmooth.reset(safeRate, 0.02);
    outputSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(
        state.getRawParameterValue("outputGain")->load()));
    peaks[0].store(0.f); peaks[1].store(0.f);
    rms[0].store(0.f); rms[1].store(0.f);
    setLatencySamples(0);
    SkellyTrace::checkpoint("processor: prepareToPlay EXIT");
}

void SkellyEQAudioProcessor::processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n=audio.getNumSamples();
    if(n<=0) return;
    if(audio.getNumChannels()<2) { audio.clear(); return; }
    for(int channel=2;channel<audio.getNumChannels();++channel)
        audio.clear(channel,0,n);

    const auto all=bands();
    const int slope=1+(int)state.getRawParameterValue("slope")->load();
    const int mode=(int)state.getRawParameterValue("stereoMode")->load();
    const bool modeChanged=mode!=lastMode;
    bool anySolo=false;
    for(const auto& band:all) anySolo |= band.enabled && band.solo;

    // A short coefficient ramp is independent of the DAW's buffer size.
    // Topology/routing changes reset old filter memory before reconfiguration,
    // preventing stale state bursts when a bypassed band is re-enabled.
    const int rampSamples=juce::jmax(1,(int)std::round(sampleRateHz()*0.008));
    for(int i=0;i<8;++i)
    {
        const auto& band=all[(size_t)i];
        const auto& old=previous[(size_t)i];
        const bool topology=modeChanged || band.type!=old.type ||
            band.channel!=old.channel || band.enabled!=old.enabled || slope!=lastSlope;
        const bool changed=topology || band.freq!=old.freq ||
            band.gain!=old.gain || band.q!=old.q;
        if(!changed) continue;
        const int ty=internalType(band.type);
        const int stages=(ty==3||ty==4)?slope:(ty==7?2:1);
        for(int channel=0;channel<2;++channel)
            for(int section=0;section<4;++section)
            {
                auto& filter=filters[i].filt[channel][section];
                if(topology) filter.reset();
                const Coeff next=section>=stages ? Coeff{}
                    : ty==7 ? tiltSection(section,band.freq,band.gain,band.q,sampleRateHz())
                    : design(ty,band.freq,band.gain,
                             (ty==3||ty==4)&&stages>1 ? cutoffQ(section,stages) : band.q,
                             sampleRateHz());
                filter.setCoefficients(next, rampSamples, topology || lastSlope<0);
            }
        previous[(size_t)i]=band;
    }
    lastSlope=slope;
    lastMode=mode;

    const float target=juce::Decibels::decibelsToGain(
        state.getRawParameterValue("outputGain")->load());
    outputSmooth.setTargetValue(target);
    const int analysis=(int)state.getRawParameterValue("analyzeMode")->load();
    float peak[2]{0.f,0.f};
    double sum[2]{0.0,0.0};
    auto* left=audio.getWritePointer(0);
    auto* right=audio.getWritePointer(1);

    for(int j=0;j<n;++j)
    {
        double L=std::isfinite(left[j]) ? left[j] : 0.0;
        double R=std::isfinite(right[j]) ? right[j] : 0.0;
        if(analysis==1) analyzer.push((float)((L+R)*.5));
        double v[2]{L,R};
        if(mode==1)
        {
            v[0]=(L+R)*.7071067811865476;
            v[1]=(L-R)*.7071067811865476;
        }
        for(int i=0;i<8;++i)
        {
            const auto& b=all[(size_t)i];
            if(!b.enabled || (anySolo && !b.solo)) continue;
            const int t=internalType(b.type);
            const int sections=(t==3||t==4)?slope:(t==7?2:1);
            for(int ch=0;ch<2;++ch)
            {
                const bool route=mode==0
                    ? (b.channel==0 || (b.channel==1 && ch==0) || (b.channel==2 && ch==1))
                    : (b.channel==0 || (b.channel==3 && ch==0) || (b.channel==4 && ch==1));
                if(!route) continue;
                for(int k=0;k<sections;++k)
                    v[ch]=filters[i].filt[ch][k].tick(v[ch]);
            }
        }
        if(mode==1)
        {
            L=(v[0]+v[1])*.7071067811865476;
            R=(v[0]-v[1])*.7071067811865476;
        }
        else {L=v[0];R=v[1];}
        const double gain=outputSmooth.getNextValue();
        if(!std::isfinite(L) || !std::isfinite(R))
        {
            L=R=0.0;
            for(auto& band:filters)
                for(auto& channel:band.filt)
                    for(auto& filter:channel) filter.reset();
        }
        // Last-resort host protection, not a creative limiter. Never expose
        // NaN/Inf or out-of-range float data to FL Studio.
        left[j]=(float)juce::jlimit(-32.0,32.0,L*gain);
        right[j]=(float)juce::jlimit(-32.0,32.0,R*gain);
        peak[0]=juce::jmax(peak[0],std::abs(left[j]));
        peak[1]=juce::jmax(peak[1],std::abs(right[j]));
        sum[0]+=(double)left[j]*left[j];
        sum[1]+=(double)right[j]*right[j];
        if(analysis>=2) analyzer.push((left[j]+right[j])*.5f);
    }
    for(int ch=0;ch<2;++ch)
    {
        peaks[ch].store(peak[ch],std::memory_order_relaxed);
        rms[ch].store((float)std::sqrt(sum[ch]/(double)n),std::memory_order_relaxed);
    }
}
void SkellyEQAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    SkellyTrace::checkpoint("state: SAVE ENTER");
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, dest);
    SkellyTrace::checkpoint("state: SAVE EXIT");
}
void SkellyEQAudioProcessor::setStateInformation(const void* data, int bytes)
{
    SkellyTrace::checkpoint("state: RESTORE ENTER");
    if (data == nullptr || bytes <= 0 || bytes > 8 * 1024 * 1024)
    {
        SkellyTrace::checkpoint("state: RESTORE SKIPPED (no data or invalid size)");
        return;
    }

    auto xml = getXmlFromBinary(data, bytes);
    if (xml == nullptr || ! xml->hasTagName(state.state.getType()))
    {
        SkellyTrace::checkpoint("state: RESTORE SKIPPED (invalid XML)");
        return;
    }
    SkellyTrace::checkpoint("state: XML parsed");
    const auto restored = juce::ValueTree::fromXml(*xml);
    if (! restored.isValid() || restored.getType() != state.state.getType())
    {
        SkellyTrace::checkpoint("state: RESTORE SKIPPED (invalid tree)");
        return;
    }

    // Whitelist parameter IDs and finite values instead of redirecting APVTS to
    // unchecked host settings. This retains older valid preset values while
    // discarding unknown child types and ill-formed parameter data.
    auto clean = state.copyState();
    for (int i = clean.getNumChildren() - 1; i >= 0; --i)
        if (clean.getChild(i).getType() != juce::Identifier("PARAM"))
            clean.removeChild(i, nullptr);

    for (int i = 0; i < restored.getNumChildren(); ++i)
    {
        const auto child = restored.getChild(i);
        if (child.getType() != juce::Identifier("PARAM"))
            continue;
        const auto paramID = child.getProperty("id").toString();
        auto* parameter = state.getParameter(paramID);
        if (parameter == nullptr || ! child.hasProperty("value"))
            continue;

        const auto rawValue = (float) child.getProperty("value");
        if (! std::isfinite(rawValue))
            continue;
        const auto normalized = parameter->convertTo0to1(rawValue);
        if (! std::isfinite(normalized))
            continue;
        const auto safeValue = parameter->convertFrom0to1(juce::jlimit(0.0f, 1.0f, normalized));
        auto destination = clean.getChildWithProperty("id", paramID);
        if (destination.isValid() && std::isfinite(safeValue))
            destination.setProperty("value", safeValue, nullptr);
    }
    if (restored.hasProperty("selectedBand"))
        clean.setProperty("selectedBand", juce::jlimit(0, 7, (int) restored.getProperty("selectedBand")), nullptr);

    // Replace snapshot nodes rather than retaining snapshots from a previous
    // FL Studio preset that does not contain any. Older presets stay valid.
    for (const char* key : {"snapshotA", "snapshotB"})
        if (auto prior = clean.getChildWithName(juce::Identifier(key)); prior.isValid())
            clean.removeChild(prior, nullptr);
    // Retain saved A/B curves across project reloads, validating all values.
    for (const char* key : {"snapshotA", "snapshotB"})
    {
        const auto incoming = restored.getChildWithName(juce::Identifier(key));
        if (!incoming.isValid()) continue;
        juce::ValueTree snapshot {juce::Identifier(key)};
        if (auto previousSnapshot = clean.getChildWithName(juce::Identifier(key)); previousSnapshot.isValid())
            clean.removeChild(previousSnapshot, nullptr);
        for (int i = 0; i < incoming.getNumChildren(); ++i)
        {
            auto node = incoming.getChild(i);
            if (node.getType() != juce::Identifier("PARAM") || !node.hasProperty("value")) continue;
            const auto paramId = node.getProperty("id").toString();
            if (auto* parameter = state.getParameter(paramId))
            {
                const float raw = (float)node.getProperty("value");
                if (!std::isfinite(raw)) continue;
                const float norm = parameter->convertTo0to1(raw);
                if (!std::isfinite(norm)) continue;
                juce::ValueTree safe {juce::Identifier("PARAM")};
                safe.setProperty("id", paramId, nullptr);
                safe.setProperty("value", parameter->convertFrom0to1(juce::jlimit(0.f, 1.f, norm)), nullptr);
                snapshot.addChild(safe, -1, nullptr);
            }
        }
        clean.addChild(snapshot, -1, nullptr);
    }
    SkellyTrace::checkpoint("state: APVTS replace ENTER");
    state.replaceState(clean);
    SkellyTrace::checkpoint("state: RESTORE EXIT");
}
void SkellyEQAudioProcessor::storeSnapshot(int slot)
{
    if (slot != 0 && slot != 1) return;
    const juce::Identifier key(slot == 0 ? "snapshotA" : "snapshotB");
    juce::ValueTree saved(key);
    auto source = state.copyState();
    // Only copy known PARAM children. Never nest an existing snapshot inside
    // another snapshot (which previously inflated host save data each time).
    for (int i = 0; i < source.getNumChildren(); ++i)
        if (source.getChild(i).getType() == juce::Identifier("PARAM"))
            saved.addChild(source.getChild(i).createCopy(), -1, nullptr);
    if (auto old = state.state.getChildWithName(key); old.isValid())
        state.state.removeChild(old, nullptr);
    state.state.addChild(saved, -1, nullptr);
}
void SkellyEQAudioProcessor::loadSnapshot(int slot)
{
    if (slot != 0 && slot != 1) return;
    const juce::Identifier key(slot == 0 ? "snapshotA" : "snapshotB");
    auto stored = state.state.getChildWithName(key);
    if (!stored.isValid()) return;
    for (int i = 0; i < stored.getNumChildren(); ++i)
    {
        auto node = stored.getChild(i);
        if (node.getType() != juce::Identifier("PARAM") || !node.hasProperty("value")) continue;
        const auto paramId = node.getProperty("id").toString();
        if (auto* target = state.getParameter(paramId))
        {
            const float value = (float)node.getProperty("value");
            if (!std::isfinite(value)) continue;
            const float normalized = target->convertTo0to1(value);
            if (!std::isfinite(normalized)) continue;
            target->beginChangeGesture();
            target->setValueNotifyingHost(juce::jlimit(0.f, 1.f, normalized));
            target->endChangeGesture();
        }
    }
}
juce::AudioProcessorEditor* SkellyEQAudioProcessor::createEditor()
{
    SkellyTrace::checkpoint("editor: createEditor ENTER");
#if SKELLY_DIAGNOSTIC_EDITOR
    struct MinimalEditor final : juce::AudioProcessorEditor
    {
        explicit MinimalEditor(juce::AudioProcessor& p) : juce::AudioProcessorEditor(p)
        {
            SkellyTrace::checkpoint("minimal-editor: constructor ENTER");
            setSize(400, 130);
            SkellyTrace::checkpoint("minimal-editor: constructor EXIT");
        }
        ~MinimalEditor() override { SkellyTrace::checkpoint("minimal-editor: destructor"); }
        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black);
            g.setColour(juce::Colours::lightgreen);
            g.drawFittedText("SKELLY EQ v1.7 - MINIMAL DIAGNOSTIC",
                             getLocalBounds(), juce::Justification::centred, 1);
        }
    };
    auto* editor = new MinimalEditor(*this);
#elif SKELLY_RESTORED_EDITOR
    auto* editor = new SkellyEQRestoredEditor(*this);
#elif SKELLY_SAFE_EDITOR
    auto* editor = new SkellyEQStableEditor(*this);
#else
    auto* editor = new SkellyEQAudioProcessorEditor(*this);
#endif
    SkellyTrace::checkpoint("editor: createEditor EXIT");
    return editor;
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new SkellyEQAudioProcessor();}
