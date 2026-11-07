#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "StableEditor.h"
#include "RestoredEditor.h"
#include "CrashTrace.h"
#include <cmath>
#include <thread>
using namespace SkellyDSP;
static juce::String id(int i,const char* n){return "b"+juce::String(i+1)+"_"+n;}
SkellyEQAudioProcessor::SkellyEQAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    linearEngine=std::make_unique<StereoLinearFIR>();
    state.addParameterListener("phaseMode",this);
    SkellyTrace::checkpoint("processor: constructor EXIT");
}
SkellyEQAudioProcessor::~SkellyEQAudioProcessor()
{
    releaseResources();
    state.removeParameterListener("phaseMode",this);
    cancelPendingUpdate();
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
// The worker owns all expensive 8192-point magnitude design + FIR FFT setup.
// It polls APVTS atomics and publishes a complete kernel via a lock-free,
// one-producer/one-consumer hand-off. Neither the worker nor the audio thread
// ever waits on a mutex held by the other.
class SkellyEQAudioProcessor::DesignerThread final : public juce::Thread {
public:
    explicit DesignerThread(SkellyEQAudioProcessor& p)
        : Thread("Skelly EQ FIR designer"), owner(p) {}
    void run() override {
        std::array<BandConfig,8> lastBands{};
        int lastSlope=-1, lastMode=-1;
        while (!threadShouldExit()) {
            if (owner.observedPhase.load(std::memory_order_relaxed)==0) {
                const auto current=owner.bands();
                const int slope=1+(int)owner.state.getRawParameterValue("slope")->load();
                const int mode=(int)owner.state.getRawParameterValue("stereoMode")->load();
                bool changed=slope!=lastSlope || mode!=lastMode;
                for (int i=0;i<8;++i) {
                    const auto& a=current[(size_t)i];
                    const auto& b=lastBands[(size_t)i];
                    changed |= a.enabled!=b.enabled || a.solo!=b.solo ||
                        a.type!=b.type || a.channel!=b.channel ||
                        a.freq!=b.freq || a.gain!=b.gain || a.q!=b.q;
                }
                if (changed) {
                    // Claim either the empty slot (0) or an unconsumed ready
                    // slot (1). Replacing stale ready data matters when the
                    // host changes a preset while stopped: first audio must
                    // not briefly play the previous settings.
                    int expected=0;
                    bool claimed=owner.linearEngine->published.compare_exchange_strong(
                        expected,3,std::memory_order_acq_rel);
                    if (!claimed) {
                        expected=1;
                        claimed=owner.linearEngine->published.compare_exchange_strong(
                            expected,3,std::memory_order_acq_rel);
                    }
                    if (claimed) {
                        StereoLinearFIR::buildKernel(current,mode,slope,owner.sampleRateHz(),
                                                      owner.linearEngine->workerKernel);
                        lastBands=current; lastSlope=slope; lastMode=mode;
                        owner.linearEngine->published.store(1,std::memory_order_release);
                    }
                }
            }
            wait(35);
        }
    }
private:
    SkellyEQAudioProcessor& owner;
};

void SkellyEQAudioProcessor::releaseResources() {
    if (designer) { designer->signalThreadShouldExit(); designer->stopThread(5000); designer.reset(); }
}

void SkellyEQAudioProcessor::parameterChanged(const juce::String& id, float value) {
    if (id=="phaseMode") {
        observedPhase.store((int)value,std::memory_order_release);
        triggerAsyncUpdate();
    }
}

void SkellyEQAudioProcessor::handleAsyncUpdate() {
    // JUCE host latency notifications go through the message thread, never
    // from the audio callback. The phase parameter may change during playback.
    const int phase=observedPhase.load(std::memory_order_acquire);
    const int desired=phase==0 ? StereoLinearFIR::latency : 0;
    if (getLatencySamples()!=desired) setLatencySamples(desired);
}

void SkellyEQAudioProcessor::prepareToPlay(double sr,int)
{
    SkellyTrace::checkpoint("processor: prepareToPlay ENTER");
    releaseResources();
    for (auto& stage : filters)
        for (auto& channel : stage.filt)
            for (auto& filter : channel)
            {
                filter.reset();
                filter.setCoefficients(Coeff{}, 0, true);
            }
    for (auto& stage : retiredFilters)
        for (auto& channel : stage.filt)
            for (auto& filter : channel)
            {
                filter.reset();
                filter.setCoefficients(Coeff{}, 0, true);
            }
    for (int i = 0; i < 8; ++i)
    {
        topologyMix[i].reset(1.0);
        retiredStages[i] = 0;
    }
    lastSlope = -1;
    lastMode = -1;
    activeStereoMode=(int)state.getRawParameterValue("stereoMode")->load();
    previousStereoMode=activeStereoMode;
    stereoFade.reset(1.0); stereoTransition=false;
    // Initialize the lane wet/dry values at the real parameter state. This
    // avoids an unwanted fade-in each time FL Studio starts playback.
    const auto initialBands = bands();
    const int initialMode = (int)state.getRawParameterValue("stereoMode")->load();
    bool initialSolo = false;
    for (const auto& b : initialBands) initialSolo |= b.enabled && b.solo;
    for (int i = 0; i < 8; ++i)
        for (int lane = 0; lane < 2; ++lane)
            bandMix[i][lane].reset(initialBands[(size_t)i].enabled &&
                (!initialSolo || initialBands[(size_t)i].solo) &&
                routesToLane(initialBands[(size_t)i], initialMode, lane) ? 1.0 : 0.0);
    const double safeRate = (std::isfinite(sr) && sr >= 8000.0) ? sr : 44100.0;
    outputSmooth.reset(safeRate, 0.02);
    outputSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(
        state.getRawParameterValue("outputGain")->load()));
    peaks[0].store(0.f); peaks[1].store(0.f);
    rms[0].store(0.f); rms[1].store(0.f);
    linearEngine->reset();
    const auto initialPhase=(int)state.getRawParameterValue("phaseMode")->load();
    observedPhase.store(initialPhase,std::memory_order_release);
    // prepareToPlay is non-realtime: building the very first FIR here gives
    // restored FL Studio projects a fully defined filter before audio starts.
    auto first=std::make_unique<StereoLinearFIR::Kernel>();
    StereoLinearFIR::buildKernel(initialBands,initialMode,
        1+(int)state.getRawParameterValue("slope")->load(),safeRate,*first);
    linearEngine->setInitialKernel(*first);
    setLatencySamples(initialPhase==0 ? StereoLinearFIR::latency : 0);
    designer=std::make_unique<DesignerThread>(*this);
    designer->startThread(juce::Thread::Priority::low);
    SkellyTrace::checkpoint("processor: prepareToPlay EXIT");
}

void SkellyEQAudioProcessor::processMinimumSample(
    double inL, double inR, int mode, Stage (&banks)[8], Stage (&retired)[8],
    WetDryRamp (&topology)[8], WetDryRamp (&wet)[8][2], const int* stages, const int* retiredCounts,
    double& outL, double& outR) noexcept
{
    double v[2]{inL,inR};
    if (mode==1) {
        v[0]=(inL+inR)*StereoLinearFIR::invSqrt2;
        v[1]=(inL-inR)*StereoLinearFIR::invSqrt2;
    }
    for (int i=0;i<8;++i) {
        const double morph=topology[i].next();
        for (int lane=0;lane<2;++lane) {
            const double mix=wet[i][lane].next();
            if (mix<=0.0) continue;
            const double dry=v[lane];
            double processed=dry;
            for (int k=0;k<stages[i];++k)
                processed=banks[i].filt[lane][k].tick(processed);
            if (morph<1.0) {
                double prior=dry;
                for (int k=0;k<retiredCounts[i];++k)
                    prior=retired[i].filt[lane][k].tick(prior);
                processed=prior+(processed-prior)*morph;
            }
            v[lane]=dry+mix*(processed-dry);
        }
    }
    if (mode==1) {
        outL=(v[0]+v[1])*StereoLinearFIR::invSqrt2;
        outR=(v[0]-v[1])*StereoLinearFIR::invSqrt2;
    } else { outL=v[0]; outR=v[1]; }
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
    const int requestedMode=(int)state.getRawParameterValue("stereoMode")->load();
    const int phase=(int)state.getRawParameterValue("phaseMode")->load();
    if (phase!=observedPhase.exchange(phase,std::memory_order_acq_rel)) triggerAsyncUpdate();
    if (linearEngine) linearEngine->consumePublished();
    // A new mode request during an existing crossfade is deferred until that
    // crossfade completes, avoiding a discontinuous restart mid-transition.
    if (!stereoTransition && activeStereoMode!=requestedMode) {
        previousStereoMode=activeStereoMode;
        for (int i=0;i<8;++i) {
            switchFilters[i]=filters[i];
            switchRetiredFilters[i]=retiredFilters[i];
            switchTopology[i]=topologyMix[i];
            switchRetiredStages[i]=retiredStages[i];
            for(int lane=0;lane<2;++lane) switchBandMix[i][lane]=bandMix[i][lane];
            const int type=internalType(previous[i].type);
            switchStageCounts[i]=(type==3||type==4)?juce::jmax(1,lastSlope)
                                  :(type==7?2:1);
            for (int lane=0;lane<2;++lane)
                for (auto& section:filters[i].filt[lane]) section.reset();
        }
        activeStereoMode=requestedMode;
        stereoFade.reset(0.0);
        stereoFade.setTarget(true,juce::jmax(1,(int)std::round(sampleRateHz()*0.02)));
        stereoTransition=true;
    }
    const int mode=activeStereoMode;
    bool anySolo=false;
    for(const auto& band:all) anySolo |= band.enabled && band.solo;

    // A short coefficient ramp is independent of the DAW's buffer size.
    // Pure bypass/solo/routing changes must NOT reset biquad memory, because
    // the old wet signal is still fading out. Only filter-type/slope changes
    // need a topology reset; coefficient edits use an eight-ms ramp.
    const int rampSamples=juce::jmax(1,(int)std::round(sampleRateHz()*0.008));
    const int bypassRampSamples=juce::jmax(1,(int)std::round(sampleRateHz()*0.006));
    const int topologyRampSamples=juce::jmax(1,(int)std::round(sampleRateHz()*0.012));
    for (int i = 0; i < 8; ++i)
        for (int lane = 0; lane < 2; ++lane)
        {
            const auto& band = all[(size_t)i];
            bandMix[i][lane].setTarget(band.enabled &&
                (!anySolo || band.solo) && routesToLane(band,mode,lane),
                bypassRampSamples);
        }
    // Filter types and stage counts are block-constant. Precompute outside
    // the per-sample loop, especially useful in dense low-buffer sessions.
    int stageCounts[8]{};
    for (int i = 0; i < 8; ++i)
    {
        const int t = internalType(all[(size_t)i].type);
        stageCounts[i] = (t == 3 || t == 4) ? slope : (t == 7 ? 2 : 1);
    }
    for(int i=0;i<8;++i)
    {
        const auto& band=all[(size_t)i];
        const auto& old=previous[(size_t)i];
        const int oldType=internalType(old.type);
        const int ty=internalType(band.type);
        // Slope only changes filter topology for cut filters. Do not
        // reinitialize peaks or shelves when the global slope is edited.
        const bool topology=lastSlope<0 || band.type!=old.type ||
            ((ty==3||ty==4) && slope!=lastSlope);
        const bool changed=topology || band.freq!=old.freq ||
            band.gain!=old.gain || band.q!=old.q;
        previous[(size_t)i]=band;
        if(!changed) continue;
        const int stages=(ty==3||ty==4)?slope:(ty==7?2:1);
        if (topology && lastSlope >= 0)
        {
            // Save the old TDF-II memory and coefficients (without heap
            // allocations). The retired bank fades away over 12 ms.
            // On a second topology change mid-fade we switch to the
            // most recently active bank, bounding DSP and state storage.
            retiredFilters[i]=filters[i];
            retiredStages[i]=(oldType==3||oldType==4)?lastSlope:
                             (oldType==7?2:1);
            topologyMix[i].reset(0.0);
            topologyMix[i].setTarget(true,topologyRampSamples);
        }
        else if (lastSlope < 0)
        {
            retiredStages[i]=0;
            topologyMix[i].reset(1.0);
        }
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
        if (phase==0 && linearEngine) {
            linearEngine->process(L,R,L,R);
        } else {
            double mainL=0.0, mainR=0.0;
            processMinimumSample(L,R,mode,filters,retiredFilters,
                topologyMix,bandMix,stageCounts,retiredStages,mainL,mainR);
            if (stereoTransition) {
                double oldL=0.0, oldR=0.0;
                processMinimumSample(L,R,previousStereoMode,
                    switchFilters,switchRetiredFilters,switchTopology,
                    switchBandMix,switchStageCounts,switchRetiredStages,oldL,oldR);
                const double alpha=stereoFade.next();
                L=oldL+(mainL-oldL)*alpha;
                R=oldR+(mainR-oldR)*alpha;
                if (alpha>=1.0) stereoTransition=false;
            } else { L=mainL; R=mainR; }
        }
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
    observedPhase.store((int)state.getRawParameterValue("phaseMode")->load(),
                        std::memory_order_release);
    triggerAsyncUpdate();
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
