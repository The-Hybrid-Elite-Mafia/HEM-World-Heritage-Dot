// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#pragma once
#include <array>
#include <cstddef>
namespace whic_legacy {
// These controls shape recorded PCM; they do not recover physical properties of
// the recorded instrument. No oscillator, random source or excitation generator.
struct DspParameters {float resonance=0, tension=0, strike=1, friction=0;};
DspParameters clampDspParameters(DspParameters p) noexcept;
class SampleDsp {
public:
 SampleDsp() noexcept {reset(48000);}
 void reset(float sampleRate) noexcept;
 // Call on the owning render thread; cross-thread UI changes must be queued by host.
 void setParameters(const DspParameters&) noexcept;
 void process(float* interleavedStereo,std::size_t frames) noexcept;
 // Optional host pitch resampling ratio (+/- 6 semitones). Filtering inside this
 // class changes brightness; it never changes pitch by inventing a new source.
 static float tensionPitchRatio(float tension) noexcept;
private:
 struct Mode {float b0=0,b2=0,a1=0,a2=0,tb0=0,tb2=0,ta1=0,ta2=0;std::array<float,2> z1{},z2{};};
 std::array<Mode,3> modes_{};
 std::array<float,2> low_{},contactLow_{};
 DspParameters target_{},current_{};
 float rate_=48000,smooth_=0,cutoff_=0,cutoffTarget_=0;
};
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.

#include <algorithm>
#include <cmath>
namespace whic_legacy {
namespace {
float finiteClamp(float v,float low,float high,float fallback) noexcept {return std::isfinite(v)?std::clamp(v,low,high):fallback;}
float state(float v) noexcept {if(!std::isfinite(v))return 0;return std::abs(v)<1e-18f?0.f:std::clamp(v,-8.f,8.f);}
constexpr float pi=3.14159265358979323846f;
}
inline DspParameters clampDspParameters(DspParameters p) noexcept {return {finiteClamp(p.resonance,0,1,0),finiteClamp(p.tension,-1,1,0),finiteClamp(p.strike,0,2,1),finiteClamp(p.friction,0,1,0)};}
inline float SampleDsp::tensionPitchRatio(float t) noexcept {return std::exp2(finiteClamp(t,-1,1,0)*0.5f);}
inline void SampleDsp::reset(float rate) noexcept {
 rate_=finiteClamp(rate,8000,384000,48000);smooth_=1.f-std::exp(-1.f/(0.01f*rate_));
 current_=target_=DspParameters{};low_.fill(0);contactLow_.fill(0);modes_={};setParameters(target_);
 cutoff_=cutoffTarget_;for(auto& m:modes_){m.b0=m.tb0;m.b2=m.tb2;m.a1=m.ta1;m.a2=m.ta2;}
}
inline void SampleDsp::setParameters(const DspParameters& p) noexcept {
 target_=clampDspParameters(p);float ratio=tensionPitchRatio(target_.tension);
 const float frequencies[]={180,540,1450};
 for(std::size_t i=0;i<modes_.size();++i){auto& m=modes_[i];float f=std::min(frequencies[i]*ratio,rate_*0.2f);float w=2*pi*f/rate_;float q=2+target_.resonance*13;float alpha=std::sin(w)/(2*q),norm=1/(1+alpha);
  // Constant peak bandpass biquad: damped modes only excited by sample input.
  m.tb0=alpha*norm;m.tb2=-m.tb0;m.ta1=-2*std::cos(w)*norm;m.ta2=(1-alpha)*norm;
 }
 float cutoff=std::min(4000*ratio,rate_*0.4f);cutoffTarget_=1-std::exp(-2*pi*cutoff/rate_);
}
inline void SampleDsp::process(float* pcm,std::size_t frames) noexcept {
 if(!pcm)return;
 for(std::size_t f=0;f<frames;++f){
  current_.resonance+=(target_.resonance-current_.resonance)*smooth_;
  current_.tension+=(target_.tension-current_.tension)*smooth_;
  current_.strike+=(target_.strike-current_.strike)*smooth_;
  current_.friction+=(target_.friction-current_.friction)*smooth_;
  cutoff_+=(cutoffTarget_-cutoff_)*smooth_;
  for(auto& m:modes_){m.b0+=(m.tb0-m.b0)*smooth_;m.b2+=(m.tb2-m.b2)*smooth_;m.a1+=(m.ta1-m.a1)*smooth_;m.a2+=(m.ta2-m.a2)*smooth_;}
  for(unsigned ch=0;ch<2;++ch){float x=finiteClamp(pcm[f*2+ch],-4,4,0);float driven=x*current_.strike;
   // Strike intensity: gain and soft compression only above neutral intensity.
   float hard=std::max(0.f,current_.strike-1);driven=driven/(1+hard*std::abs(driven)*0.6f);
   float modal=0;for(auto& m:modes_){float y=state(m.b0*driven+m.z1[ch]);m.z1[ch]=state(-m.a1*y+m.z2[ch]);m.z2[ch]=state(m.b2*driven-m.a2*y);modal+=y;}
   low_[ch]=state(low_[ch]+cutoff_*(driven-low_[ch]));
   // Negative tension darkens; positive tension emphasizes high-frequency content.
   float shaped=driven+std::abs(current_.tension)*0.55f*(current_.tension<0?low_[ch]-driven:driven-low_[ch]);
   contactLow_[ch]=state(contactLow_[ch]+0.08f*(driven-contactLow_[ch]));float contact=driven-contactLow_[ch];
   // Friction-like contact texture is an amplitude-dependent nonlinear residual
   // from the recorded signal, not noise synthesis or a physical bow-pressure model.
   float residual=contact/(1+3*std::abs(contact))-contact;
   float out=shaped+0.65f*current_.resonance*modal+current_.friction*residual;
   pcm[f*2+ch]=finiteClamp(out,-4,4,0);
  }
 }
}
}
