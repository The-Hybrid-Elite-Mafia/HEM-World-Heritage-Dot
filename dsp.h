#pragma once
#include <array>
#include <cstddef>
namespace whic {
// Legacy presets retain the original post-mix sample processor via legacyMode.
struct DspParameters {float resonance=0,tension=0,strike=1,friction=0;float hardness=.5f,strikeWindowMs=40,detuneCents=0,stiffness=.25f,decaySeconds=1.5f,sympathetic=.2f,releaseNoise=.5f,blend=0;bool physicsOn=true;bool legacyMode=false;};
DspParameters clampDspParameters(DspParameters) noexcept;
class SampleDsp {
public: SampleDsp() noexcept {reset(48000);} void reset(float) noexcept;void setParameters(const DspParameters&) noexcept;void setFundamental(float hz) noexcept;void process(float*,std::size_t) noexcept;static float tensionPitchRatio(float) noexcept;
private:
 float rate_=48000,hz_=220,smooth_=.002f;DspParameters target_{},current_{};
 std::array<std::array<float,16384>,2> delay_{};unsigned cursor_=0;unsigned long long age_=0;
 alignas(32) std::array<float,8> coeff_{},radius_{},inputGain_{};
 alignas(32) std::array<std::array<float,8>,2> y1_{},y2_{};
 std::array<float,2> previous_{},damped_{},legacyLow_{};void coefficients() noexcept;
};
}
