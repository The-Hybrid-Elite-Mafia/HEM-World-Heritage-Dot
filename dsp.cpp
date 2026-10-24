#include "dsp.h"
#include <algorithm>
#include <cmath>
#include <immintrin.h>
namespace whic {
namespace {float clamp(float v,float a,float z,float d){return std::isfinite(v)?std::clamp(v,a,z):d;}constexpr float pi=3.14159265358979323846f;float bounded(float v){return std::isfinite(v)?std::clamp(v,-8.f,8.f):0.f;}}
DspParameters clampDspParameters(DspParameters p) noexcept {p.resonance=clamp(p.resonance,0,1,0);p.tension=clamp(p.tension,-1,1,0);p.strike=clamp(p.strike,0,2,1);p.friction=clamp(p.friction,0,1,0);p.hardness=clamp(p.hardness,0,1,.5f);p.strikeWindowMs=clamp(p.strikeWindowMs,1,250,40);p.detuneCents=clamp(p.detuneCents,-50,50,0);p.stiffness=clamp(p.stiffness,0,1,.25f);p.decaySeconds=clamp(p.decaySeconds,.05f,12,1.5f);p.sympathetic=clamp(p.sympathetic,0,1,.2f);p.releaseNoise=clamp(p.releaseNoise,0,1,.5f);p.blend=clamp(p.blend,0,1,0);return p;}
float SampleDsp::tensionPitchRatio(float t) noexcept{return std::exp2(clamp(t,-1,1,0)*.5f);}
void SampleDsp::reset(float rate) noexcept{rate_=clamp(rate,8000,384000,48000);smooth_=1-std::exp(-1/(.01f*rate_));target_=current_=DspParameters{};for(auto& x:delay_)x.fill(0);for(auto& x:y1_)x.fill(0);for(auto& x:y2_)x.fill(0);previous_.fill(0);damped_.fill(0);legacyLow_.fill(0);cursor_=0;age_=0;coefficients();}
void SampleDsp::setFundamental(float hz) noexcept {hz_=clamp(hz,27.5f,4186,220);coefficients();}
void SampleDsp::setParameters(const DspParameters& p) noexcept {target_=clampDspParameters(p);coefficients();}
void SampleDsp::coefficients() noexcept {for(unsigned k=0;k<8;++k){float harmonic=float(k+1);float freq=std::min(rate_*.42f,hz_*harmonic*std::sqrt(1+target_.stiffness*.006f*harmonic*harmonic));float r=std::exp(-6.907755f/(target_.decaySeconds*rate_*(1+.08f*k)));radius_[k]=r*r;coeff_[k]=2*r*std::cos(2*pi*freq/rate_);inputGain_[k]=(1-r)*(.8f/ harmonic);}}
void SampleDsp::process(float* pcm,std::size_t frames) noexcept {if(!pcm)return;for(size_t f=0;f<frames;++f){
#define SM(v) current_.v+=(target_.v-current_.v)*smooth_
 SM(resonance);SM(tension);SM(strike);SM(friction);SM(hardness);SM(strikeWindowMs);SM(detuneCents);SM(stiffness);SM(decaySeconds);SM(sympathetic);SM(releaseNoise);SM(blend);
#undef SM
 float window=std::max(1.f,current_.strikeWindowMs*.001f*rate_);float envelope=std::max(0.f,1-float(age_)/window);float delay=std::clamp(rate_/(hz_*std::exp2(current_.detuneCents/1200)),2.f,16380.f);unsigned n=unsigned(delay);float frac=delay-n;float feedback=std::min(.9999f,std::pow(.001f,delay/(current_.decaySeconds*rate_)));
 for(unsigned ch=0;ch<2;++ch){float x=clamp(pcm[f*2+ch],-4,4,0),driven=x*current_.strike;float hard=std::max(0.f,current_.strike-1);driven/=1+hard*std::abs(driven)*.6f;legacyLow_[ch]+=.12f*(driven-legacyLow_[ch]);float dry=driven+std::abs(current_.tension)*.55f*(current_.tension<0?legacyLow_[ch]-driven:driven-legacyLow_[ch]);float excitation=(driven+(driven-previous_[ch])*current_.hardness*2)*envelope;previous_[ch]=driven;
 unsigned a=(cursor_+16384-n)%16384,z=(a+16383)%16384;float delayed=delay_[ch][a]*(1-frac)+delay_[ch][z]*frac;damped_[ch]+=(.9f-.7f*current_.stiffness)*(delayed-damped_[ch]);delay_[ch][cursor_]=bounded(excitation*.35f+feedback*damped_[ch]);
 // Eight parallel damped modes; real sample is the only excitation. AVX2, no noise/oscillator.
 __m256 y=_mm256_add_ps(_mm256_mul_ps(_mm256_load_ps(coeff_.data()),_mm256_load_ps(y1_[ch].data())),_mm256_mul_ps(_mm256_load_ps(inputGain_.data()),_mm256_set1_ps(excitation+driven*current_.sympathetic*.1f)));y=_mm256_sub_ps(y,_mm256_mul_ps(_mm256_load_ps(radius_.data()),_mm256_load_ps(y2_[ch].data())));y=_mm256_min_ps(_mm256_set1_ps(8),_mm256_max_ps(_mm256_set1_ps(-8),y));_mm256_store_ps(y2_[ch].data(),_mm256_load_ps(y1_[ch].data()));_mm256_store_ps(y1_[ch].data(),y);float modal=0;for(float v:y1_[ch])modal+=v;
 float wet=bounded(excitation*.55f+damped_[ch]*(.2f+.8f*current_.resonance)+modal*(.5f+current_.sympathetic));float mix=target_.physicsOn?current_.blend:0;pcm[f*2+ch]=bounded(dry*(1-mix)+wet*mix);
 }cursor_=(cursor_+1)%16384;++age_;}}
}
