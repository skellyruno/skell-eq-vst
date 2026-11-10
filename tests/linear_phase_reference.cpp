#include "../Source/dsp/LinearPhaseFIR.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>
using namespace SkellyDSP;

// A reference *time-domain* convolution catches circular wrap, channel
// crossfeed, lost overlap, and off-by-one latency errors. These assertions
// intentionally do not reuse the production overlap-add implementation.
static std::array<std::vector<double>,4> unpackImpulse(const StereoLinearFIR::Kernel& k) {
    std::array<std::vector<double>,4> h;
    for (int entry=0;entry<4;++entry) {
        h[(size_t)entry].resize(StereoLinearFIR::impulseSize, 0.0);
        if (!k.active[(size_t)entry]) continue;
        for (int part=0;part<StereoLinearFIR::partitions;++part) {
            auto bins=k.matrix[(size_t)entry][(size_t)part];
            StereoLinearFIR::fft(bins,true);
            for(int i=0;i<StereoLinearFIR::partition;++i) {
                const int tap=part*StereoLinearFIR::partition+i;
                if(tap<StereoLinearFIR::impulseSize)
                    h[(size_t)entry][(size_t)tap]=bins[(size_t)i].real();
            }
        }
    }
    return h;
}
int main() {
    for(int stereoMode: {0,1}) {
        for(int sr: {44100,48000,96000}) {
            std::array<BandConfig,8> bands{};
            for(auto& b:bands) b.enabled=false;
            bands[0].enabled=true; bands[0].type=0;
            bands[0].channel=stereoMode ? 3 : 1; // Mid or Left only
            bands[0].freq=950.; bands[0].gain=12.; bands[0].q=2.5;
            bands[1].enabled=true; bands[1].type=1;
            bands[1].channel=0; bands[1].freq=130.;
            bands[1].gain=-3.; bands[1].q=.7;
            auto k=std::make_unique<StereoLinearFIR::Kernel>();
            StereoLinearFIR::buildKernel(bands,stereoMode,4,sr,*k);
            if(stereoMode==0) assert(!k->active[1]&&!k->active[2]);
            if(stereoMode==1) assert(k->active[1]&&k->active[2]);
            const auto h=unpackImpulse(*k);
            // Compare the actual windowed FIR magnitude to the expected
            // eight-band IIR target where the fixed FIR has sufficient
            // frequency resolution (this intentionally excludes sub-100Hz).
            for(double freq: {250.0,950.0,2000.0,9000.0}) {
                std::complex<double> actual{0.0,0.0};
                std::complex<double> phase{1.0,0.0};
                const auto step=std::polar(1.0,-2.0*3.14159265358979323846*freq/sr);
                for(const double tap:h[0]) {actual+=phase*tap;phase*=step;}
                std::complex<double> lane[2]{{1.0,0.0},{1.0,0.0}};
                for(const auto& b:bands)
                    if(b.enabled)
                        for(int ch=0;ch<2;++ch)
                            if(routesToLane(b,stereoMode,ch))
                                lane[ch]*=bandResponse(b,freq,sr,4);
                const double expected=stereoMode==0 ? std::abs(lane[0])
                                    : 0.5*(std::abs(lane[0])+std::abs(lane[1]));
                const double errorDb=20.0*std::log10(std::abs(actual)/expected);
                if(std::abs(errorDb)>0.7) {
                    std::cerr<<"FIR magnitude mismatch "<<freq<<" Hz, sr "<<sr
                             <<" mode "<<stereoMode<<": "<<errorDb<<" dB\n";
                    return 2;
                }
            }
            auto engine=std::make_unique<StereoLinearFIR>();
            engine->reset(); engine->setInitialKernel(*k);
            constexpr int n=3900;
            std::array<std::vector<double>,2> input,got;
            for(int lane=0;lane<2;++lane) {
                input[(size_t)lane].resize(n);got[(size_t)lane].resize(n);
            }
            for(int i=0;i<n;++i) {
                input[0][(size_t)i]=0.35*std::sin(0.071*i)+0.11*std::cos(0.029*i);
                input[1][(size_t)i]=0.19*std::cos(0.113*i)-0.31*std::sin(0.037*i);
                double l=0,r=0;
                engine->process(input[0][(size_t)i],input[1][(size_t)i],l,r);
                got[0][(size_t)i]=l;got[1][(size_t)i]=r;
            }
            double maxError=0.0;
            for(int i=0;i<n;++i) {
                for(int dst=0;dst<2;++dst) {
                    double expected=0.0;
                    const int lastTap=std::min(StereoLinearFIR::impulseSize-1,
                                               i-StereoLinearFIR::partition);
                    for(int src=0;src<2;++src) {
                        const auto& impulse=h[(size_t)(dst*2+src)];
                        for(int tap=0;tap<=lastTap;++tap)
                            expected+=impulse[(size_t)tap]*input[(size_t)src][(size_t)(i-StereoLinearFIR::partition-tap)];
                    }
                    const double actual=got[(size_t)dst][(size_t)i];
                    assert(std::isfinite(actual));
                    maxError=std::max(maxError,std::abs(actual-expected));
                }
            }
            if(maxError>1.0e-8) {
                std::cerr<<"Reference mismatch "<<stereoMode<<" at "<<sr<<" Hz: "<<maxError<<"\n";
                return 1;
            }
            std::cout<<"PASS reference convolution "<<sr<<" Hz mode="<<stereoMode
                     <<" maxError="<<maxError<<"\n";
        }
    }
    // Both M/S EQ lanes identical: matrix crossfeed must be exactly zero.
    std::array<BandConfig,8> bands{};
    for(auto& b:bands) b.enabled=false;
    bands[0].enabled=true;bands[0].channel=0;
    auto k=std::make_unique<StereoLinearFIR::Kernel>();
    StereoLinearFIR::buildKernel(bands,1,4,48000,*k);
    assert(!k->active[1] && !k->active[2]);
    std::cout<<"PASS redundant M/S crossfeed elimination\n";
}
