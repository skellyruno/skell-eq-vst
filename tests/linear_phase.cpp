#include "../Source/dsp/LinearPhaseFIR.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
using namespace SkellyDSP;
int main() {
    auto engine=std::make_unique<StereoLinearFIR>();
    auto kernel=std::make_unique<StereoLinearFIR::Kernel>();
    std::array<BandConfig,8> bands{};
    for (auto& b:bands) b.enabled=false;
    StereoLinearFIR::buildKernel(bands,0,4,48000,*kernel);
    engine->reset(); engine->setInitialKernel(*kernel);
    const double eps=2.0e-7;
    for(int i=0;i<5000;++i) {
      double l=0,r=0;
      engine->process(i==0 ? 1.0 : 0.0,0.0,l,r);
      if (i==StereoLinearFIR::latency) {
        if (std::abs(l-1.0)>eps) {std::cerr<<"gain="<<l<<" at "<<i<<"\n";return 1;}
      } else if(std::abs(l)>eps) { std::cerr<<"unexpected left at "<<i<<" value "<<l<<"\n";return 2; }
      if(std::abs(r)>eps) {std::cerr<<"unexpected right at "<<i<<" value "<<r<<"\n";return 3;}
    }
    // M/S-only Mid boost generates equal, phase-coherent crossfeed L->R.
    bands[0].enabled=true; bands[0].type=0; bands[0].channel=3;
    bands[0].freq=1200.; bands[0].gain=9.; bands[0].q=1.;
    StereoLinearFIR::buildKernel(bands,1,4,48000,*kernel);
    engine->reset(); engine->setInitialKernel(*kernel);
    double impulseL[5000]{},impulseR[5000]{};
    for(int i=0;i<5000;++i) {
      double l=0,r=0;
      engine->process(i==0 ? 1.0 : 0.0,0.0,l,r);
      impulseL[i]=l; impulseR[i]=r;
      assert(std::isfinite(l) && std::isfinite(r));
    }
    // Each matrix FIR must be symmetric about the reported total latency.
    for(int k=1;k<500;++k) {
      assert(std::abs(impulseL[StereoLinearFIR::latency+k]-impulseL[StereoLinearFIR::latency-k])<1e-8);
      assert(std::abs(impulseR[StereoLinearFIR::latency+k]-impulseR[StereoLinearFIR::latency-k])<1e-8);
    }
    assert(impulseR[StereoLinearFIR::latency]>0);
    // Simulate lock-free worker publication during streaming. Test both an
    // in-flight FIR morph and an opposite stereo routing change.
    engine->reset(); engine->setInitialKernel(*kernel);
    bands[0].channel=4; // Side-only: opposite matrix cross-feed sign
    StereoLinearFIR::buildKernel(bands,1,4,48000,engine->workerKernel);
    engine->published.store(1,std::memory_order_release);
    engine->consumePublished();
    assert(engine->published.load(std::memory_order_acquire)==0);
    double lastL=0.0,lastR=0.0;
    for (int i=0;i<8000;++i) {
        double l=0.0,r=0.0;
        engine->process(0.15*std::sin(2.0*3.141592653589793*314.0*i/48000.0),
                        -0.1*std::cos(2.0*3.141592653589793*814.0*i/48000.0),l,r);
        assert(std::isfinite(l) && std::isfinite(r));
        assert(std::abs(l)<3.0 && std::abs(r)<3.0);
        if(i>2000) {assert(std::abs(l-lastL)<1.0 && std::abs(r-lastR)<1.0);}
        lastL=l;lastR=r;
    }
    // FIR history must not replay samples from a previous linear-mode session.
    for (int i=0;i<5000;++i) {
        double l=0,r=0;
        engine->process(0.25,-0.18,l,r);
    }
    engine->clearStreamingHistory();
    for (int i=0;i<4500;++i) {
        double l=999.0,r=999.0;
        engine->process(0.0,0.0,l,r);
        if (!std::isfinite(l) || !std::isfinite(r) ||
            std::abs(l)>1e-10 || std::abs(r)>1e-10) {
            std::cerr<<"stale FIR audio leaked after returning from minimum phase at "<<i<<"\n";
            return 4;
        }
    }
    std::cout<<"linear phase impulse/PDC, stereo matrix, morph and clean re-entry passed\n";
}
