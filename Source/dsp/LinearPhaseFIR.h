#pragma once
// Skelly EQ linear-phase engine: a 2049-tap real symmetric FIR designed from
// per-channel magnitude responses and streamed by 128-sample uniform FFT
// partitioned convolution. No allocation, locks, or impulse FFT on audio thread.
// 1024-sample FIR group delay + 128-sample block latency = 1152 samples PDC.
#include "BiquadCascade.h"
#include <array>
#include <atomic>
#include <complex>
#include <cmath>
#include <algorithm>
#include <memory>

namespace SkellyDSP {
class StereoLinearFIR {
public:
    using Complex = std::complex<double>;
    static constexpr int partition = 128;
    static constexpr int fftSize = partition * 2;
    static constexpr int impulseSize = 2049;
    static constexpr int partitions = (impulseSize + partition - 1) / partition;
    static constexpr int designSize = 8192;
    static constexpr int groupDelay = (impulseSize - 1) / 2;
    static constexpr int latency = groupDelay + partition;
    static constexpr double invSqrt2 = 0.7071067811865475244;
    using Spectrum = std::array<Complex, fftSize>;
    struct Kernel {
        // (output channel * 2 + input channel), partition, frequency bin.
        // L/R mode has no crossfeed; when M/S lanes have identical responses,
        // the off-diagonal matrix terms vanish too. Skip these paths entirely
        // while retaining a fixed-size, allocation-free kernel handoff.
        std::array<std::array<Spectrum, partitions>, 4> matrix{};
        std::array<bool, 4> active{{true, true, true, true}};
    };

    // Iterative power-of-two complex FFT; inverse uses 1/N normalization.
    template <size_t N> static void fft(std::array<Complex, N>& data, bool inverse) noexcept {
        for (size_t i=1, j=0; i<N; ++i) {
            size_t bit=N>>1;
            while (j & bit) { j ^= bit; bit >>= 1; }
            j ^= bit;
            if (i < j) std::swap(data[i], data[j]);
        }
        for (size_t length=2; length<=N; length<<=1) {
            const double a = (inverse ? 2.0 : -2.0) * 3.1415926535897932384626433832795 / double(length);
            const Complex step(std::cos(a), std::sin(a));
            for (size_t i=0; i<N; i+=length) {
                Complex w(1.0, 0.0);
                for (size_t j=0; j<length/2; ++j) {
                    const Complex u=data[i+j], v=data[i+j+length/2]*w;
                    data[i+j]=u+v;
                    data[i+j+length/2]=u-v;
                    w*=step;
                }
            }
        }
        if (inverse) for (auto& x:data) x/=double(N);
    }

    static void buildKernel(const std::array<BandConfig,8>& bands,
                            int stereoMode, int slope, double sampleRate,
                            Kernel& result) {
        const double sr=std::isfinite(sampleRate) && sampleRate>=8000.0 ? sampleRate : 44100.0;
        auto spectra = std::make_unique<std::array<std::array<Complex, designSize>, 4>>();
        bool solo=false;
        bool hasCrossfeed=false;
        for (const auto& b:bands) solo |= b.enabled && b.solo;
        for (int bin=0; bin<=designSize/2; ++bin) {
            // Never query an IIR response above its frequency's Nyquist bin.
            const double freq=double(bin)*sr/double(designSize);
            double magnitude[2]{1.0,1.0};
            for (int lane=0;lane<2;++lane) {
                double m=1.0;
                for (const auto& b:bands) {
                    if (!b.enabled || (solo && !b.solo) || !routesToLane(b,stereoMode,lane)) continue;
                    const auto response=bandResponse(b,freq,sr,slope);
                    m *= std::abs(response);
                }
                magnitude[lane]=std::isfinite(m) ? std::clamp(m,0.0,32.0) : 1.0;
            }
            // Magnitudes are the same for L/R with no cross routing, and for
            // M/S if both processing lanes have the same EQ. In both cases
            // the off-diagonal matrix FIRs are identically zero.
            hasCrossfeed |= stereoMode != 0 && magnitude[0] != magnitude[1];
            const double matrix[4] = {
                stereoMode==0 ? magnitude[0] : 0.5*(magnitude[0]+magnitude[1]),
                stereoMode==0 ? 0.0 : 0.5*(magnitude[0]-magnitude[1]),
                stereoMode==0 ? 0.0 : 0.5*(magnitude[0]-magnitude[1]),
                stereoMode==0 ? magnitude[1] : 0.5*(magnitude[0]+magnitude[1])
            };
            for (int entry=0;entry<4;++entry) {
                (*spectra)[entry][(size_t)bin]=Complex(matrix[entry],0.0);
                if (bin>0 && bin<designSize/2)
                    (*spectra)[entry][designSize-(size_t)bin]=Complex(matrix[entry],0.0);
            }
        }
        result.active = {{true, hasCrossfeed, hasCrossfeed, true}};
        for (int entry=0;entry<4;++entry) {
            if (!result.active[(size_t)entry]) {
                // Make the inactive coefficients deterministic for tests and
                // debug inspection; process() skips these lanes entirely.
                for (auto& stage : result.matrix[(size_t)entry]) stage.fill(Complex{});
                continue;
            }
            fft((*spectra)[entry],true);
            for (int part=0;part<partitions;++part) {
                Spectrum stage{};
                for (int n=0;n<partition;++n) {
                    const int idx=part*partition+n;
                    if (idx>=impulseSize) break;
                    const int offset=idx-groupDelay;
                    const int wrapped=offset>=0 ? offset : designSize+offset;
                    // Blackman window mitigates truncation-induced ringing.
                    const double phase=2.0*3.14159265358979323846*double(idx)/double(impulseSize-1);
                    const double window=0.42-0.5*std::cos(phase)+0.08*std::cos(2.0*phase);
                    stage[(size_t)n]=Complex((*spectra)[entry][(size_t)wrapped].real()*window,0.0);
                }
                fft(stage,false);
                result.matrix[(size_t)entry][(size_t)part]=stage;
            }
        }
    }

    void reset() noexcept {
        for (auto& ch: inputSpectra) for (auto& p:ch) p.fill(Complex{});
        for (auto& ch:inputBlock) ch.fill(0.0);
        for (auto& ch:ready) ch.fill(0.0);
        for (auto& ch:oldReady) ch.fill(0.0);
        for (auto& ch:overlap) ch.fill(0.0);
        for (auto& ch:oldOverlap) ch.fill(0.0);
        ringSlot=0; position=0; fade.reset(1.0); fading=false;
        pending=false; published.store(0,std::memory_order_release);
    }
    void setInitialKernel(const Kernel& k) noexcept { current=k; pending=false; fading=false; }

    // 0=empty, 1=ready, 2=audio copying, 3=worker writing.
    // The worker may atomically claim 0 or replace unconsumed state 1;
    // audio claims only ready state 1, then releases 2 -> 0.
    // prepare/teardown stop the worker before reset().
    std::atomic<int> published{0};
    Kernel workerKernel{};

    void consumePublished() noexcept {
        int expected=1;
        if (published.compare_exchange_strong(expected,2,std::memory_order_acq_rel)) {
            nextKernel=workerKernel;
            pending=true;
            published.store(0,std::memory_order_release);
        }
    }

    void process(double inLeft,double inRight,double& outLeft,double& outRight) noexcept {
        const double mix=fading ? fade.next() : 1.0;
        outLeft=ready[0][(size_t)position];
        outRight=ready[1][(size_t)position];
        if (fading) {
            outLeft=oldReady[0][(size_t)position]*(1.0-mix)+outLeft*mix;
            outRight=oldReady[1][(size_t)position]*(1.0-mix)+outRight*mix;
            if (mix>=1.0) fading=false;
        }
        inputBlock[0][(size_t)position]=std::isfinite(inLeft) ? inLeft : 0.0;
        inputBlock[1][(size_t)position]=std::isfinite(inRight) ? inRight : 0.0;
        if (++position==partition) {
            position=0;
            for (int ch=0;ch<2;++ch) {
                Spectrum frame{};
                for (int i=0;i<partition;++i) frame[(size_t)i]=inputBlock[(size_t)ch][(size_t)i];
                fft(frame,false);
                inputSpectra[(size_t)ch][(size_t)ringSlot]=frame;
            }
            if (pending && !fading) {
                // Reconstruct previous block overlap with the NEW kernel from
                // existing input history, so no zero-filled warm-up gap occurs.
                const int prev=(ringSlot+partitions-1)%partitions;
                std::array<std::array<double,partition>,2> prior{};
                std::array<std::array<double,partition>,2> discard{};
                calculateBlock(nextKernel,prev,discard.data(),prior.data());
                previousTail=prior;
                old=current; oldOverlap=overlap;
                current=nextKernel;
                overlap=previousTail;
                pending=false;
                fade.reset(0.0);
                fade.setTarget(true,partition*8); // 1024 samples (~21ms at 48k)
                fading=true;
            }
            if (fading)
                calculateBlock(old,ringSlot,oldReady.data(),oldOverlap.data());
            calculateBlock(current,ringSlot,ready.data(),overlap.data());
            ringSlot=(ringSlot+1)%partitions;
        }
        if (!std::isfinite(outLeft)) outLeft=0.0;
        if (!std::isfinite(outRight)) outRight=0.0;
    }
private:
    // Return block convolution, with outgoing overlap written into tails.
    void calculateBlock(const Kernel& kernel,int slot,
                        std::array<double,partition>* output,
                        std::array<double,partition>* tails) noexcept {
        for (int dst=0;dst<2;++dst) {
            Spectrum result{};
            for (int src=0;src<2;++src) {
                const int matrix=dst*2+src;
                if (!kernel.active[(size_t)matrix]) continue;
                for (int p=0;p<partitions;++p) {
                    const int index=(slot+partitions-p)%partitions;
                    const auto& x=inputSpectra[(size_t)src][(size_t)index];
                    const auto& h=kernel.matrix[(size_t)matrix][(size_t)p];
                    // Input and kernel FFTs are conjugate-symmetric because
                    // their time-domain blocks are real. Work only on the
                    // non-redundant N/2+1 bins, then mirror before the IFFT.
                    for (int bin=0;bin<=partition;++bin)
                        result[(size_t)bin] += x[(size_t)bin]*h[(size_t)bin];
                }
            }
            for (int bin=1;bin<partition;++bin)
                result[(size_t)(fftSize-bin)] = std::conj(result[(size_t)bin]);
            fft(result,true);
            for (int i=0;i<partition;++i) {
                const double prior=tails!=nullptr ? tails[dst][(size_t)i] : 0.0;
                output[dst][(size_t)i]=result[(size_t)i].real()+prior;
                if (tails!=nullptr) tails[dst][(size_t)i]=result[(size_t)(i+partition)].real();
            }
        }
    }
    Kernel current{},old{},nextKernel{};
    std::array<std::array<Spectrum,partitions>,2> inputSpectra{};
    std::array<std::array<double,partition>,2> inputBlock{},ready{},oldReady{},overlap{},oldOverlap{},previousTail{};
    WetDryRamp fade{};
    int position=0,ringSlot=0;
    bool pending=false,fading=false;
};
} // namespace SkellyDSP
