// SPDX-License-Identifier: GPL-3.0-only
#define NOMINMAX
#include "audio.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <set>
#include <algorithm>
using namespace whic; namespace fs=std::filesystem;
int wmain(int argc,wchar_t**argv){
 if(argc!=3)return 2;
 std::vector<Sample> pack;std::wstring provenance,error;DspParameters dsp;
 if(!loadPreset(argv[1],pack,provenance,error,&dsp)){std::wcerr<<error;return 3;}
 std::ofstream report(fs::path(argv[2]),std::ios::binary);report<<"zone\troot\tlow\thigh\tvel_low\tvel_high\trr\tfinite\tnonzero\tpeak\n";
 for(auto&s:pack)if(!prepareForPlayback(s,error)){std::wcerr<<error;return 4;}
 unsigned failures=0;
 for(size_t i=0;i<pack.size();++i){const auto&s=pack[i];int key=std::clamp(s.map.root,s.map.low,s.map.high),vel=(s.map.velLow+s.map.velHigh)/2;OfflineRender r;
  if(!AudioEngine::renderOffline({s},provenance,true,{{0,key,vel}},48000,96000,r,error,dsp)){std::wcerr<<error;return 5;}
  bool finite=true;double peak=0;for(float v:r.stereo){finite=finite&&std::isfinite(v);if(std::isfinite(v))peak=std::max(peak,std::abs(double(v)));}
  bool nonzero=peak>1e-9;bool selected=r.selections.size()==1&&r.selections[0].sampleIndex==0;
  if(!finite||!nonzero||!selected)++failures;
  report<<i<<'\t'<<s.map.root<<'\t'<<s.map.low<<'\t'<<s.map.high<<'\t'<<s.map.velLow<<'\t'<<s.map.velHigh<<'\t'<<s.map.rr<<'\t'<<finite<<'\t'<<nonzero<<'\t'<<peak<<'\n';
 }
 unsigned combinations=0;std::set<int> covered;
 for(int key=0;key<128;++key)for(int velocity=1;velocity<=127;++velocity){std::set<int> expected;
  for(size_t i=0;i<pack.size();++i){const auto&m=pack[i].map;if(key>=m.low&&key<=m.high&&velocity>=m.velLow&&velocity<=m.velHigh)expected.insert(int(i));}
  std::vector<OfflineNote> events;size_t n=std::max<size_t>(1,expected.size());for(size_t j=0;j<n;++j)events.push_back({uint32_t(j),key,velocity});
  OfflineRender r;if(!AudioEngine::renderOffline(pack,provenance,true,events,48000,uint32_t(n+1),r,error,dsp)){std::wcerr<<error;return 6;}
  std::set<int> actual;for(auto&selection:r.selections)if(selection.sampleIndex>=0)actual.insert(selection.sampleIndex);
  if(actual!=expected||r.selections.size()!=n)++failures;
  for(float v:r.stereo)if(!std::isfinite(v)){++failures;break;}
  covered.insert(actual.begin(),actual.end());++combinations;
 }
 if(covered.size()!=pack.size())++failures;
 std::cout<<"zones="<<pack.size()<<" individual_zone_renders="<<pack.size()<<" exhaustive_key_velocity_combinations="<<combinations<<" full_pack_RR_zones="<<covered.size()<<" failures="<<failures<<" audio_device_calls=0\n";
 return failures?1:0;
}
