// Compiles the real filter header with a tiny portable JUCE-math shim.
// It cannot replace DAW/host testing, but catches filter regressions quickly.
#include "../Source/dsp/BiquadCascade.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    using namespace SkellyDSP;
    constexpr double sr=48000.0;
    const auto p=design(0,1000.0,0.0,1.0,sr);
    for(double hz : {20.0, 200.0, 1000.0, 9000.0, 20000.0})
        assert(std::abs(std::abs(p.response(juce::MathConstants<double>::twoPi*hz/sr))-1.0) < 1.0e-8);
    const auto hp=design(4,1000.0,0.0,0.70710678,sr);
    const auto lp=design(3,1000.0,0.0,0.70710678,sr);
    assert(std::abs(hp.response(0.0)) < 1.0e-7);
    assert(std::abs(lp.response(juce::MathConstants<double>::pi)) < 1.0e-7);
    const auto high=juce::MathConstants<double>::twoPi*10000.0/sr;
    assert(std::abs(hp.response(high)) > 0.9);
    assert(std::abs(lp.response(high)) < 0.05);
    for(int slope=1;slope<=4;++slope)
        for(int k=0;k<slope;++k)
            assert(std::isfinite(cutoffQ(k,slope)) && cutoffQ(k,slope)>0.0);
    // Lane routing shared by the DSP and the visual response; verify each mode.
    for (int mode = 0; mode < 2; ++mode)
        for (int assign = 0; assign < 5; ++assign)
        {
            BandConfig routed;
            routed.channel = assign;
            for (int lane = 0; lane < 2; ++lane)
            {
                const bool expected = mode == 0
                    ? (assign == 0 || (assign == 1 && lane == 0) || (assign == 2 && lane == 1))
                    : (assign == 0 || (assign == 3 && lane == 0) || (assign == 4 && lane == 1));
                assert(routesToLane(routed, mode, lane) == expected);
            }
        }
    // Transitions complete in exactly N samples, remain within [0,1],
    // and can be reversed mid-ramp without a single-sample discontinuity.
    WetDryRamp fader;
    fader.reset(0.0);
    fader.setTarget(true, 288);
    double last = 0.0;
    for (int i=0; i<96; ++i) {
        const double value=fader.next();
        assert(value > last && value >= 0.0 && value <= 1.0);
        last=value;
    }
    const double beforeReverse = fader.value();
    fader.setTarget(false, 288);
    const double firstReverse = fader.next();
    assert(firstReverse < beforeReverse && firstReverse > 0.0);
    for (int i=0; i<287; ++i) {
        const double value=fader.next();
        assert(value >= 0.0 && value <= 1.0);
    }
    assert(fader.value() == 0.0 && !fader.active());
    fader.setTarget(true, 288);
    for (int i=0; i<288; ++i) fader.next();
    assert(fader.value() == 1.0 && fader.active());
    fader.setTarget(false, 0);
    assert(fader.value() == 0.0);
    // Blend real filter output while rapidly toggling the wet/dry target.
    Biquad transitionFilter;
    transitionFilter.setCoefficients(design(0, 1200, 9.0, 2.0, sr), 0, true);
    fader.reset(0.0);
    for (int sample=0; sample<50000; ++sample) {
        if (sample % 517 == 0) fader.setTarget((sample / 517) % 2 == 0, 288);
        const double dry=0.3*std::sin(juce::MathConstants<double>::twoPi*730.0*sample/sr);
        const double mix=fader.next();
        const double wet=transitionFilter.tick(dry);
        const double out=dry+mix*(wet-dry);
        assert(std::isfinite(out) && std::abs(out) < 5.0);
    }
    // Exercise the same old/new topology morph used by processBlock.
    // A resonant peak changes to high-pass while audio is playing; the old
    // filter state should not be abruptly discarded at the block boundary.
    Biquad previousTopology, nextTopology;
    previousTopology.setCoefficients(design(0, 1200.0, 15.0, 8.0, sr), 0, true);
    nextTopology.setCoefficients(design(4, 1200.0, 0.0, 0.7071, sr), 0, true);
    WetDryRamp topologyFade;
    topologyFade.reset(1.0);
    for (int i=0; i<4096; ++i) {
        const double x=0.12*std::sin(juce::MathConstants<double>::twoPi*1200.0*i/sr);
        previousTopology.tick(x); // fill the outgoing filter's TDF-II memory
    }
    topologyFade.reset(0.0);
    topologyFade.setTarget(true, 576);  // 12 ms at 48 kHz
    double lastMorph=0.0;
    for (int i=0; i<576; ++i) {
        const double x=0.12*std::sin(juce::MathConstants<double>::twoPi*1200.0*(i+4096)/sr);
        const double alpha=topologyFade.next();
        const double oldWet=previousTopology.tick(x);
        const double newWet=nextTopology.tick(x);
        const double value=oldWet+(newWet-oldWet)*alpha;
        assert(std::isfinite(value) && std::abs(value)<25.0);
        assert(alpha >= lastMorph && alpha<=1.0);
        lastMorph=alpha;
    }
    assert(topologyFade.value()==1.0);
    for(int type=0;type<8;++type)
        assert(filterUsesGain(type) == (type==0||type==1||type==2||type==7));
    Biquad filter;
    filter.setCoefficients(design(0,800.0,9.0,2.0,sr),0,true);
    double sum=0;
    for(int i=0;i<250000;++i) {
        if(i%1700==0) {
            const double freq=100.0+(double)(i%19000);
            const double gain=(i%3400==0) ? 12.0 : -12.0;
            filter.setCoefficients(design(0,freq,gain,1.25,sr),384);
        }
        const double out=filter.tick(0.3*std::sin(juce::MathConstants<double>::twoPi*997.0*i/sr));
        assert(std::isfinite(out) && std::abs(out)<100.0);
        sum+=out*out;
    }
    assert(std::isfinite(sum) && sum>0.0);
    BandConfig tilt;
    tilt.type=7; tilt.freq=1000.0; tilt.gain=6.0; tilt.q=0.707;
    auto tiltLow=std::abs(bandResponse(tilt,60.0,sr,1));
    auto tiltHigh=std::abs(bandResponse(tilt,12000.0,sr,1));
    assert(tiltLow < 1.0 && tiltHigh > 1.0);
    for(int uiType=0;uiType<8;++uiType) {
        const auto coeff=design(internalType(uiType),19000.0,18.0,.1,44100.0);
        Biquad b; b.setCoefficients(coeff,0,true);
        for(int i=0;i<16384;++i)
            assert(std::isfinite(b.tick(i==0 ? 1.0 : 0.0)));
    }
    std::cout << "Skelly EQ DSP smoke tests PASS\n";
}
