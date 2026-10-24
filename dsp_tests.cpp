// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#include "dsp.h"
#include <array>
#include <vector>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <limits>
#include <chrono>
#include <cstring>
#include <algorithm>
// Entirely offline. Reads real recorded WAV, never opens audio or MIDI devices.
unsigned u16(const unsigned char*p){return p[0]|unsigned(p[1])<<8;}
unsigned u32(const unsigned char*p){return u16(p)|(u16(p+2)<<16);}
int wmain(int argc,wchar_t**argv){
 if(argc!=2){std::cerr<<"Supply the real FreePats guitar C4.wav path\n";return 2;}
 std::ifstream file(std::filesystem::path(argv[1]),std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
 if(bytes.size()<44||std::memcmp(bytes.data(),"RIFF",4)||std::memcmp(bytes.data()+8,"WAVE",4))return 3;
 unsigned channels=0,rate=0,bits=0;std::size_t offset=0,length=0;
 for(std::size_t p=12;p+8<=bytes.size();){unsigned n=u32(bytes.data()+p+4);if(n>bytes.size()-p-8)return 4;if(!std::memcmp(bytes.data()+p,"fmt ",4)&&n>=16){if(u16(bytes.data()+p+8)!=1)return 5;channels=u16(bytes.data()+p+10);rate=u32(bytes.data()+p+12);bits=u16(bytes.data()+p+22);}if(!std::memcmp(bytes.data()+p,"data",4)){offset=p+8;length=n;}p+=8+n+(n&1);}
 if(bits!=16||channels!=1||!rate||!length)return 6;
 std::vector<float> source(length/2*2);for(std::size_t i=0;i<length/2;++i){auto v=static_cast<short>(u16(bytes.data()+offset+i*2));source[i*2]=source[i*2+1]=v/32768.f;}
 int failures=0;auto check=[&](bool ok,const char* s){std::cout<<(ok?"PASS: ":"FAIL: ")<<s<<'\n';failures+=!ok;};
 auto run=[&](whic::DspParameters p){auto out=source;whic::SampleDsp dsp;dsp.reset(float(rate));dsp.setParameters(p);dsp.process(out.data(),out.size()/2);return out;};
 auto difference=[&](const auto& a){double e=0;for(std::size_t i=0;i<a.size();++i)e+=std::abs(a[i]-source[i]);return e/a.size();};
 auto dry=run({});check(dry==source,"neutral DSP exactly preserves real sample PCM");
 for(auto p:{whic::DspParameters{1,0,1,0},{0,1,1,0},{0,-1,1,0},{0,0,1.8f,0},{0,0,1,1}}){auto out=run(p);auto delta=difference(out);std::cout<<"Mean absolute real-sample difference="<<delta<<'\n';check(delta>1e-5,"control changes real recorded audio offline");}
 whic::SampleDsp dsp;dsp.reset(float(rate));dsp.setParameters({1,1,2,1});std::vector<float> silence(rate*2);dsp.process(silence.data(),silence.size()/2);check(std::all_of(silence.begin(),silence.end(),[](float f){return f==0;}),"maximum controls cannot generate audio without sample excitation");
 auto p=whic::clampDspParameters({std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-100,100});check(p.resonance==0&&p.tension==0&&p.strike==0&&p.friction==1,"NaN infinity and excessive controls clamped safely");
 dsp.reset(float(rate));std::vector<float> mod=source;auto start=std::chrono::steady_clock::now();for(unsigned repeat=0;repeat<20;++repeat){mod=source;for(std::size_t i=0;i<mod.size()/2;i+=64){dsp.setParameters({float((i/64)%2),float((i/64)%3)-1,2,float((i/64)%2)});dsp.process(mod.data()+i*2,std::min<std::size_t>(64,mod.size()/2-i));}check(std::all_of(mod.begin(),mod.end(),[](float f){return std::isfinite(f)&&std::abs(f)<=4;}),"modulated resonators remain finite and bounded");}
 double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();std::cout<<"Processed "<<20*(source.size()/2)<<" stereo frames with64-frame modulation in "<<ms<<"ms; source duration per pass="<<double(source.size()/2)/rate<<"s\n";
 std::fill(silence.begin(),silence.end(),0);for(int i=0;i<10;++i){std::fill(silence.begin(),silence.end(),0);dsp.process(silence.data(),silence.size()/2);}float peak=0;for(auto f:silence)peak=std::max(peak,std::abs(f));check(peak<1e-8f,"sample-excited filter tails decay after source ends");
 check(std::abs(whic::SampleDsp::tensionPitchRatio(1)-std::sqrt(2.f))<1e-6f,"host tension resampling ratio limited to plus/minus6semitones");
 std::cout<<failures<<" failures; no audio device used.\n";return failures?1:0;
}
