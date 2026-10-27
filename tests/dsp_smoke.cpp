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
