// SPDX-License-Identifier: GPL-3.0-only
#include "audio.h"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
using namespace whic;
void stats(const std::vector<float>& data,double& peak,double& rms){peak=0;rms=0;for(float x:data){if(!std::isfinite(x))throw 1;peak=std::max(peak,std::abs(double(x)));rms+=double(x)*x;}rms=std::sqrt(rms/std::max(size_t(1),data.size()));}
int wmain(int argc,wchar_t**argv){if(argc!=2)return 2;std::cout<<"preset,source_peak,source_rms,velocity,voices,blend,output_peak,output_rms\n";for(auto&f:std::filesystem::recursive_directory_iterator(argv[1])){if(f.path().extension()!=L".whic")continue;std::vector<Sample> s;std::wstring p,e;DspParameters d;if(!loadPreset(f.path().wstring(),s,p,e,&d)||s.empty())return 3;auto z=s[s.size()/2];if(!prepareForPlayback(z,e))return 4;Decoder decoder;if(!decoder.open(z,e))return 5;std::vector<float> raw(96000*2);auto n=decoder.read(raw.data(),96000);raw.resize(n*2);double sp,sr;stats(raw,sp,sr);for(int velocity:{100,127})for(int voices:{1,8})for(float blend:{0.f,.9f}){auto map=z;map.map.velLow=1;map.map.velHigh=127;map.map.low=0;map.map.high=127;std::vector<OfflineNote> events;for(int v=0;v<voices;++v)events.push_back({0,std::clamp(map.map.root+(voices==1?0:v-4),0,127),velocity});auto settings=d;settings.blend=blend;OfflineRender out;if(!AudioEngine::renderOffline({map},p,false,events,48000,96000,out,e,settings
#ifdef FIXED_GAIN
,6.f
#endif
))return 6;double op,orr;stats(out.stereo,op,orr);std::cout<<std::quoted(f.path().stem().string())<<','<<sp<<','<<sr<<','<<velocity<<','<<voices<<','<<blend<<','<<op<<','<<orr<<'\n';}}}

