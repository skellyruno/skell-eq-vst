#pragma once
#include <JuceHeader.h>
#include <array>
#include <complex>
#include <cmath>
namespace SkellyDSP {
struct Coeff { double b0=1,b1=0,b2=0,a1=0,a2=0;
  std::complex<double> response(double w) const {auto z=std::polar(1.0,-w);return (b0+b1*z+b2*z*z)/(1.0+a1*z+a2*z*z);}
};
inline Coeff design(int type,double freq,double gain,double q,double sr) {
  freq=juce::jlimit(20.0,0.475*sr,freq);q=juce::jlimit(.1,18.0,q);
  const double w=juce::MathConstants<double>::twoPi*freq/sr, c=std::cos(w),s=std::sin(w), A=std::pow(10.0,gain/40.0), alpha=s/(2*q), root=std::sqrt(A);
  double b0=1,b1=0,b2=0,a0=1,a1=0,a2=0;
  switch(type){
    case 0: b0=1+alpha*A;b1=-2*c;b2=1-alpha*A;a0=1+alpha/A;a1=-2*c;a2=1-alpha/A;break;
    case 1: b0=A*((A+1)-(A-1)*c+2*root*alpha);b1=2*A*((A-1)-(A+1)*c);b2=A*((A+1)-(A-1)*c-2*root*alpha);a0=(A+1)+(A-1)*c+2*root*alpha;a1=-2*((A-1)+(A+1)*c);a2=(A+1)+(A-1)*c-2*root*alpha;break;
    case 2: b0=A*((A+1)+(A-1)*c+2*root*alpha);b1=-2*A*((A-1)+(A+1)*c);b2=A*((A+1)+(A-1)*c-2*root*alpha);a0=(A+1)-(A-1)*c+2*root*alpha;a1=2*((A-1)-(A+1)*c);a2=(A+1)-(A-1)*c-2*root*alpha;break;
    case 3: b0=(1-c)/2;b1=1-c;b2=(1-c)/2;a0=1+alpha;a1=-2*c;a2=1-alpha;break; // low-pass for high-cut
    case 4: b0=(1+c)/2;b1=-(1+c);b2=(1+c)/2;a0=1+alpha;a1=-2*c;a2=1-alpha;break; // high-pass for low-cut
    case 5: b0=1;b1=-2*c;b2=1;a0=1+alpha;a1=-2*c;a2=1-alpha;break;
    case 6: b0=alpha;b1=0;b2=-alpha;a0=1+alpha;a1=-2*c;a2=1-alpha;break;
    default: return design(gain>=0?2:1,freq,gain*.5,q,sr);
  }
  return {b0/a0,b1/a0,b2/a0,a1/a0,a2/a0};
}
class Biquad { public: Coeff c; double z1=0,z2=0;
 double tick(double x) noexcept {
  if (!std::isfinite(x)) { reset(); return 0.0; }
  const auto y=c.b0*x+z1;
  if (!std::isfinite(y) || std::abs(y)>1.0e8) { reset(); return 0.0; }
  const auto nextZ1=c.b1*x-c.a1*y+z2;
  const auto nextZ2=c.b2*x-c.a2*y;
  if (!std::isfinite(nextZ1) || !std::isfinite(nextZ2)) { reset(); return 0.0; }
  z1=nextZ1;z2=nextZ2;return y;
 }
 void reset() noexcept {z1=z2=0;}
};
struct BandConfig {bool enabled=true,solo=false;int type=0,channel=0;double freq=1000,gain=0,q=1;};
inline int internalType(int uiType) { return uiType==3?4:uiType==4?3:uiType; }
inline double cutoffQ(int index,int total) { const double theta=juce::MathConstants<double>::pi*(2*index+1)/(4*total);return 1.0/(2*std::cos(theta)); }
inline std::complex<double> bandResponse(const BandConfig& b,double f,double sr,int slope){if(!b.enabled)return {1,0};int t=internalType(b.type);int sections=(t==3||t==4)?slope:1;std::complex<double> r{1,0};for(int i=0;i<sections;++i)r*=design(t,b.freq,b.gain,(sections>1?cutoffQ(i,sections):b.q),sr).response(juce::MathConstants<double>::twoPi*f/sr);return r;}
}
