// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "audio.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <limits>
namespace fs=std::filesystem;using namespace whic;
namespace {
std::string utf8(const std::wstring&s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string v(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),v.data(),n,nullptr,nullptr);return v;}
}
int wmain(int argc,wchar_t**argv){
 if(argc<2||argc>3){std::wcerr<<L"Usage: zone_parameters_test <real-acoustic-preset.whic> [test-output-folder]\n";return 2;}
 unsigned checks=0,failed=0;auto check=[&](bool b,const wchar_t*label){++checks;std::wcout<<(b?L"PASS ":L"FAIL ")<<label<<L'\n';if(!b)++failed;};
 std::vector<Sample> pack;std::wstring provenance,error;if(!loadPreset(argv[1],pack,provenance,error)||pack.empty()){std::wcerr<<error;return 2;}auto original=pack.front();if(original.frames<10000||!prepareForPlayback(original,error)){std::wcerr<<L"Need a real recording with at least10000frames: "<<error;return 2;}
 fs::path dir=argc==3?fs::path(argv[2]):fs::path(L"zone-parameters-output");fs::create_directories(dir);const auto file=dir/L"roundtrip-v4.whic";
 auto sample=original;sample.map.low=0;sample.map.high=127;sample.map.velLow=1;sample.map.velHigh=127;sample.startOffset=777;sample.tuneCents=137.5f;sample.gainDb=-6.25f;sample.attackSeconds=.0125f;sample.releaseSeconds=.075f;
 check(savePreset(file.wstring(),{sample},provenance,error),L"save v4 source parameters");std::vector<Sample> loaded;std::wstring prov;check(loadPreset(file.wstring(),loaded,prov,error)&&loaded.size()==1,L"load v4 source parameters");if(loaded.size()!=1)return 3;
 check(loaded[0].startOffset==sample.startOffset&&loaded[0].tuneCents==sample.tuneCents&&loaded[0].gainDb==sample.gainDb&&loaded[0].attackSeconds==sample.attackSeconds&&loaded[0].releaseSeconds==sample.releaseSeconds,L"all source parameters roundtrip exactly");
 // Create legacy presets only in the dedicated test output directory.
 auto write=[&](const fs::path&p,int version,const std::string&tail){std::ofstream f(p,std::ios::binary);f<<"WHIC_PRESET "<<version<<'\n'<<std::quoted(utf8(provenance))<<'\n';if(version>=3)f<<"0 0 1 0\n";f<<"1\n"<<std::quoted(utf8(original.path))<<" 60 0 127 1 127 0";if(version>=2)f<<" 0.05";if(version>=4)f<<' '<<tail;f<<'\n';};
 for(int version=1;version<=3;++version){auto p=dir/(L"legacy-v"+std::to_wstring(version)+L".whic");write(p,version,"");std::vector<Sample> old;check(loadPreset(p.wstring(),old,prov,error)&&old.size()==1&&old[0].startOffset==0&&old[0].tuneCents==0&&old[0].gainDb==0&&old[0].attackSeconds==.003f,L"v1/v2/v3 migration supplies source defaults");}
 const std::vector<std::string> badRows={std::to_string(original.frames)+" 0 0 0.003","-1 0 0 0.003","0 1200.01 0 0.003","0 -1200.01 0 0.003","0 0 -96.01 0.003","0 0 60.01 0.003","0 0 0 -0.001","0 0 0 30.01","0 nan 0 0.003","0 0 inf 0.003","0 0 0 nan","0 0 0"};
 for(size_t i=0;i<badRows.size();++i){auto p=dir/(L"invalid-"+std::to_wstring(i)+L".whic");write(p,4,badRows[i]);auto prior=loaded;auto priorProv=prov;check(!loadPreset(p.wstring(),loaded,prov,error)&&loaded.size()==prior.size()&&loaded[0].startOffset==prior[0].startOffset&&prov==priorProv,L"malformed/out-of-bounds v4 rejected atomically");}
 auto bad=sample;bad.attackSeconds=31;check(!savePreset(file.wstring(),{bad},provenance,error),L"invalid save rejected");check(loadPreset(file.wstring(),loaded,prov,error)&&loaded[0].attackSeconds==sample.attackSeconds,L"invalid save preserves previous preset");
 for(auto range: {0,1,2,3}){auto edge=sample;if(range==0){edge.startOffset=original.frames-1;edge.tuneCents=-1200;edge.gainDb=-96;edge.attackSeconds=0;}if(range==1){edge.tuneCents=1200;edge.gainDb=60;edge.attackSeconds=30;}if(range==2)edge.startOffset=0;if(range==3)edge.releaseSeconds=.001f;check(validSampleParameters(edge),L"documented parameter boundary accepted");}
 // Direct PCM reference uses the unchanged real recording, explicit fractional
 // source position, dB amplitude scaling, and attack/release integration.
 for(unsigned variant=0;variant<3;++variant){auto s=sample;if(variant==1){s.attackSeconds=0;s.tuneCents=-700;s.gainDb=0;}if(variant==2){s.startOffset=2049;s.tuneCents=1200;s.gainDb=-12;s.attackSeconds=.025f;}
  int key=s.map.root;constexpr uint32_t rate=48000,off=4000,frames=12000;double step=double(s.rate)/rate*std::pow(2.,s.tuneCents/1200.);size_t needed=std::min<uint64_t>(s.frames-s.startOffset,uint64_t(std::ceil(frames*step))+2);std::vector<float> source(needed*2);Decoder decoder;bool opened=decoder.open(s,error)&&decoder.seek(s.startOffset);size_t got=opened?decoder.read(source.data(),needed):0;check(opened&&got==needed,L"reference reads exact unchanged source segment");
  OfflineRender rendered;bool ok=AudioEngine::renderOffline({s},provenance,true,{{0,key,100},{off,key,0}},rate,frames,rendered,error);check(ok,L"offline parameterized render succeeds");if(!ok)continue;double worst=0,energy=0,position=double(s.startOffset);float envelope=0,gain=.25f*100/127.f*std::pow(10.f,s.gainDb/20.f),attack=1.f/std::max(1.f,s.attackSeconds*rate),release=1.f/(s.releaseSeconds*rate);bool finite=true;
  for(size_t i=0;i<frames;++i){uint64_t absolute=uint64_t(position);float frac=float(position-absolute);size_t a=size_t(absolute-s.startOffset);if(i<off)envelope=std::min(1.f,envelope+attack);else envelope=std::max(0.f,envelope-release);for(size_t ch=0;ch<2;++ch){float expected=0;if(a<got){size_t b=std::min(a+1,got-1);expected=std::clamp((source[a*2+ch]+(source[b*2+ch]-source[a*2+ch])*frac)*gain*envelope,-1.f,1.f);}float actual=rendered.stereo[i*2+ch];finite=finite&&std::isfinite(actual);worst=std::max(worst,std::abs(double(actual)-expected));energy+=double(actual)*actual;}position+=step;}
  check(finite&&energy>1e-10&&worst<1e-6,L"offset/tuning/dB gain/attack/release match PCM reference");std::wcout<<L"REFERENCE variant="<<variant<<L" max-error="<<worst<<L" frames="<<frames<<L'\n';
 }
 auto rejected=sample;rejected.startOffset=rejected.frames;OfflineRender unused;check(!AudioEngine::renderOffline({rejected},provenance,true,{{0,60,100}},48000,512,unused,error),L"renderer rejects source-end offset");rejected=sample;rejected.gainDb=std::numeric_limits<float>::infinity();check(!AudioEngine::renderOffline({rejected},provenance,true,{{0,60,100}},48000,512,unused,error),L"renderer rejects nonfinite gain");
 std::wcout<<checks<<L" checks; "<<failed<<L" failures. Real recording PCM only; no audio device initialized or started.\n";return failed?1:0;
}
