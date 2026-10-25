// SPDX-License-Identifier: GPL-3.0-only
#include "output_stage.h"
#include <vector>
#include <iostream>
#include <limits>
using namespace whic;
int main(){std::vector<float> v(96000,.05f),original=v;OutputStage unity;unity.process(v.data(),48000,0,48000);if(v!=original||unity.limitedFrames)return 1;OutputStage boost;boost.process(v.data(),48000,6,48000);if(std::abs(v.back()/.05f-std::pow(10.f,.3f))>.001||boost.limitedFrames)return 2;std::vector<float> loud(96000);for(size_t i=0;i<loud.size();i+=2){loud[i]=8;loud[i+1]=4;}OutputStage limiter;limiter.process(loud.data(),48000,12,48000);for(size_t i=0;i<loud.size();i+=2)if(!std::isfinite(loud[i])||std::abs(loud[i])>.98001f||std::abs(loud[i+1]/loud[i]-.5f)>.0001)return 3;if(limiter.left>.98001||limiter.right>.49001||!limiter.limitedFrames)return 4;float zero[2]{};OutputStage silent;silent.process(zero,1,12,48000);if(zero[0]||zero[1])return 5;if(std::abs(meterFraction(.001f))>.0001||std::abs(meterFraction(.1f)-2.f/3)>.0001||meterFraction(1)!=1)return 6;std::cout<<"PASS unity bit exact; +6 dB measured; stereo-linked overload ceiling; silence; post-output peaks; dBFS meter scale. No device opened.\n";}
