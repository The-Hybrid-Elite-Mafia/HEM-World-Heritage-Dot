// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "sample.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
namespace fs=std::filesystem;
int wmain(int argc,wchar_t**argv){
 if(argc!=2)return 2;int total=0,failed=0;auto check=[&](bool ok,const char*label){++total;if(!ok)++failed;std::cout<<(ok?"PASS: ":"FAIL: ")<<label<<'\n';};
 whic::Sample s;s.path=fs::absolute(argv[1]).lexically_normal().wstring();s.releaseSeconds=1.5f;std::wstring error,provenance;check(whic::probeSample(s,error),"real sample metadata");std::vector<whic::Sample> samples{s},loaded;
 auto dir=fs::absolute("preset-dsp-test-output")/("run-"+std::to_string(GetCurrentProcessId()));fs::create_directories(dir);auto path=dir/"v3.whic";whic::DspParameters wanted{.3f,-.4f,1.7f,.8f},got{};
 check(whic::savePreset(path.wstring(),samples,L"Real source preset test",error,wanted),"save v3 DSP preset");check(whic::loadPreset(path.wstring(),loaded,provenance,error,&got),"load v3 DSP preset");check(got.resonance==wanted.resonance&&got.tension==wanted.tension&&got.strike==wanted.strike&&got.friction==wanted.friction,"all DSP controls roundtrip exactly");check(loaded.size()==1&&loaded[0].releaseSeconds==1.5f&&loaded[0].path==s.path,"release and original source identity preserved");
 whic::DspParameters invalid{5,-9,8,-1};check(whic::savePreset(path.wstring(),samples,L"Clamp test",error,invalid)&&whic::loadPreset(path.wstring(),loaded,provenance,error,&got),"out-of-range controls save and load");check(got.resonance==1&&got.tension==-1&&got.strike==2&&got.friction==0,"DSP saved controls clamped to supported ranges");
 invalid={std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};check(whic::savePreset(path.wstring(),samples,L"Nonfinite test",error,invalid)&&whic::loadPreset(path.wstring(),loaded,provenance,error,&got),"nonfinite controls normalized on save");check(got.resonance==0&&got.tension==0&&got.strike==1&&got.friction==0,"nonfinite controls use neutral defaults");
 for(int version:{1,2}){auto old=dir/(version==1?"v1.whic":"v2.whic");{std::ofstream f(old);f<<"WHIC_PRESET "<<version<<"\n\"legacy\"\n1\n"<<std::quoted(fs::path(s.path).string())<<" 60 0 127 1 127 0";if(version==2)f<<" 1.5";f<<'\n';}got=wanted;check(whic::loadPreset(old.wstring(),loaded,provenance,error,&got),"legacy preset migration loads");check(got.resonance==0&&got.tension==0&&got.strike==1&&got.friction==0,"legacy migration resets processing neutral");check(loaded[0].releaseSeconds==(version==1?.05f:1.5f),"legacy release time preserved");}
 auto extreme=dir/"extreme.whic";{std::ofstream f(extreme);f<<"WHIC_PRESET 3\n\"extreme\"\n9 -8 7 -6\n0\n";}check(whic::loadPreset(extreme.wstring(),loaded,provenance,error,&got),"v3 numeric extremes accepted with clamping");check(got.resonance==1&&got.tension==-1&&got.strike==2&&got.friction==0,"v3 load clamps controls");
 auto broken=dir/"broken.whic";{std::ofstream f(broken);f<<"WHIC_PRESET 3\n\"broken\"\n0 0 1 0\n1\n\"missing.wav\" 60 0 127 1 127 0 1.5\n";}got=wanted;loaded=samples;provenance=L"keep";check(!whic::loadPreset(broken.wstring(),loaded,provenance,error,&got),"failed v3 source load rejected");check(got.resonance==wanted.resonance&&got.tension==wanted.tension&&got.strike==wanted.strike&&got.friction==wanted.friction&&loaded.size()==1&&loaded[0].path==s.path&&provenance==L"keep","failed load transaction preserves DSP and instrument");
 std::cout<<total<<" checks; "<<failed<<" failures. Silent filesystem/decoder tests only.\n";return failed?1:0;
}
