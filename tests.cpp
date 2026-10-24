// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "sample.h"
#include <filesystem>
#include <iostream>
#include <cmath>
#include <fstream>
#include <array>
using namespace whic;
int failures=0, checks=0;
void check(bool ok,const char*label){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<label<<'\n';}else std::cout<<"PASS: "<<label<<'\n';}
int main(int argc,char**argv){
 auto dir=std::filesystem::absolute(argc>1?argv[1]:"test-fixtures");std::wstring error;
 auto m=inferMapping(L"fixture_C4_v1-80_rr2.wav");check(m.root==60&&m.velLow==1&&m.velHigh==80&&m.rr==2,"filename root velocity round-robin mapping");
 check(inferMapping(L"piano_F#3.wav").root==54,"sharp note mapping");check(inferMapping(L"piano_Bb2.wav").root==46,"flat note mapping");check(inferMapping(L"piano_key72.wav").root==72,"numeric key mapping");
 std::vector<Sample> good;
 for(auto name:{L"fixture_C4_v1-80_rr2.wav",L"fixture.aiff",L"fixture.flac"}){
  Sample s;s.path=(dir/name).wstring();s.map=m;bool probed=probeSample(s,error);check(probed,"probe valid WAV/AIFF/FLAC");if(!probed){std::wcerr<<s.path<<L": "<<error<<L'\n';continue;}
  check(s.rate==44100&&s.frames>=4095&&s.frames<=4096,"source metadata");Decoder d;bool opened=d.open(s,error);check(opened,"open streaming decoder");if(!opened)continue;
  std::array<float,8192> audio{};size_t n=d.read(audio.data(),4096);check(n==4096,"decode exact complete frame count");double maxError=0;bool stereo=true;
  for(size_t i=0;i<n;++i){double expected=std::round(std::sin(i*2*3.14159265358979323846/64)*12000)/32768;maxError=std::max(maxError,std::abs(audio[i*2]-expected));stereo &= audio[i*2]==audio[i*2+1];}
  check(maxError<0.00005,"decoded samples match reference PCM");check(stereo,"mono duplicated into stereo");check(d.read(audio.data(),10)==0,"end of stream bounded read");check(d.seek(0),"rewind");check(d.read(audio.data(),64)==64,"read after rewind");check(std::abs(audio[2]-1176.f/32768)<0.0001,"rewind sample accuracy");check(d.seek(777),"seek interior frame");check(d.read(audio.data(),17)==17,"read small streaming block after seek");check(std::abs(audio[0]-std::round(std::sin(777*2*3.14159265358979323846/64)*12000)/32768)<0.00005,"interior seek sample accuracy");check(!d.seek(100000),"reject out-of-range seek");good.push_back(s);
 }
 for(auto name:{L"truncated.wav",L"not_audio.wav"}){Sample bad;bad.path=(dir/name).wstring();check(!probeSample(bad,error),"reject malformed WAV");check(!error.empty(),"malformed file gives error");}
 auto preset=(dir/L"roundtrip.whic").wstring();check(savePreset(preset,good,L"SYNTHETIC TEST FIXTURES ONLY; Unicode \u266b",error),"save preset");std::vector<Sample> loaded;std::wstring provenance;check(loadPreset(preset,loaded,provenance,error),"load preset");check(loaded.size()==good.size()&&!loaded.empty(),"preset zone count");check(provenance==L"SYNTHETIC TEST FIXTURES ONLY; Unicode \u266b","UTF-8 provenance roundtrip");if(!loaded.empty())check(loaded[0].path==good[0].path&&loaded[0].map.rr==2&&loaded[0].map.velHigh==80,"preset paths and mapping roundtrip");
 auto prior=loaded.size();check(!loadPreset((dir/L"malformed.whic").wstring(),loaded,provenance,error),"reject malformed preset");check(loaded.size()==prior,"failed load preserves current instrument");
 if(!good.empty()){check(!savePreset(good[0].path,good,L"test",error),"preset save cannot overwrite source WAV");auto invalid=good;invalid[0].map.low=128;check(!savePreset(preset,invalid,L"test",error),"invalid mapping rejected before save");check(loadPreset(preset,loaded,provenance,error),"failed save preserves prior valid preset");}
 std::cout<<checks<<" checks; "<<failures<<" failures\n";return failures?1:0;
}
