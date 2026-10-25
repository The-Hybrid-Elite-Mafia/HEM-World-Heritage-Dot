// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace whic {
// Stereo-linked safety limiter, instantaneous attack and 50 ms gain recovery.
// No normalization, no lookahead, and unity below the ceiling. Original PCM is immutable.
struct OutputStage {
 float gain=1,limiter=1;uint64_t limitedFrames=0;float left=0,right=0;
 void process(float* pcm,unsigned frames,float db,unsigned rate){
  float target=std::pow(10.f,std::clamp(std::isfinite(db)?db:0.f,-24.f,12.f)/20.f);
  float smoothing=1-std::exp(-1.f/(.005f*rate)),recovery=1-std::exp(-1.f/(.05f*rate));left=right=0;
  for(unsigned i=0;i<frames;++i){gain+=(target-gain)*smoothing;float l=pcm[i*2]*gain,r=pcm[i*2+1]*gain;if(!std::isfinite(l))l=0;if(!std::isfinite(r))r=0;float peak=std::max(std::abs(l),std::abs(r));float required=peak>.98f?.98f/peak:1;limiter=required<limiter?required:limiter+(required-limiter)*recovery;if(limiter<.99999f)++limitedFrames;l*=limiter;r*=limiter;pcm[i*2]=l;pcm[i*2+1]=r;left=std::max(left,std::abs(l));right=std::max(right,std::abs(r));}
 }
};
inline float meterFraction(float peak){return peak>0?std::clamp((20.f*std::log10(peak)+60)/60,0.f,1.f):0;}
}
