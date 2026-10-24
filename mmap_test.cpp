// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "sample.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <array>
#include <algorithm>
#include <cstring>
namespace fs=std::filesystem;
unsigned u16(const unsigned char*p){return p[0]|p[1]<<8;}
uint32_t u32(const unsigned char*p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
uint64_t hash(const void*p,size_t n){auto*b=(const unsigned char*)p;uint64_t h=14695981039346656037ULL;for(size_t j=0;j<n;++j){h^=b[j];h*=1099511628211ULL;}return h;}
int wmain(int argc,wchar_t**argv){
 if(argc!=2){std::cerr<<"Usage: whic_mmap_test <real-recorded-C4.wav>\n";return 2;}
 int failures=0,checks=0;auto check=[&](bool ok,const char*name){++checks;std::cout<<(ok?"PASS: ":"FAIL: ")<<name<<'\n';if(!ok)++failures;};
 fs::path source=fs::absolute(argv[1]);std::ifstream input(source,std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});input.close();if(bytes.size()<44||memcmp(bytes.data(),"RIFF",4)){std::cerr<<"Need real WAV source.\n";return 3;}
 size_t data=0,length=0;unsigned channels=0,bits=0,rate=0,format=0;
 for(size_t p=12;p+8<=bytes.size();){uint32_t n=u32(bytes.data()+p+4);if(n>bytes.size()-p-8)return 4;if(!memcmp(bytes.data()+p,"fmt ",4)&&n>=16){format=u16(bytes.data()+p+8);channels=u16(bytes.data()+p+10);rate=u32(bytes.data()+p+12);bits=u16(bytes.data()+p+22);}if(!memcmp(bytes.data()+p,"data",4)){data=p+8;length=n;}p+=8+n+(n&1);}
 if(format!=1||bits!=16||channels<1||channels>2||!length){std::cerr<<"Independent reference expects original FreePats PCM16.\n";return 5;}
 size_t frames=length/(channels*2);auto reference=[&](uint64_t frame,int ch){size_t offset=data+(frame%frames)*channels*2+(channels==1?0:ch*2);uint16_t raw=uint16_t(u16(bytes.data()+offset));int value=raw>=32768?int(raw)-65536:int(raw);return float(value/32768.0);};
 std::wstring error;whic::Sample real;real.path=source.wstring();check(whic::probeSample(real,error),"real guitar source probes");check(real.frames==frames&&real.rate==rate,"metadata matches independent RIFF parse");uint64_t decodedHash=14695981039346656037ULL;
 {whic::Decoder d;check(d.open(real,error),"real guitar mapped decoder opens");check(d.usingMemoryMap(),"real WAV uses named memory-map streamer");std::array<float,6002> out{};uint64_t done=0;bool equal=true;
  while(size_t got=d.read(out.data(),3001)){for(size_t j=0;j<got;++j)for(int ch=0;ch<2;++ch)equal&=out[j*2+ch]==reference(done+j,ch);auto*p=(const unsigned char*)out.data();for(size_t j=0;j<got*8;++j){decodedHash^=p[j];decodedHash*=1099511628211ULL;}done+=got;}check(equal&&done==frames,"entire real PCM exactly matches independent integer reference");
  for(uint64_t at:{uint64_t(0),uint64_t(8191),uint64_t(8192),uint64_t(16383),uint64_t(frames-17)}){bool ok=d.seek(at);size_t got=d.read(out.data(),17);for(size_t j=0;j<got;++j)for(int ch=0;ch<2;++ch)ok&=out[j*2+ch]==reference(at+j,ch);check(ok&&got==17,"seek crosses double-buffer page boundary accurately");}
  check(d.seek(frames)&&d.read(out.data(),1)==0,"EOF seek stays bounded");check(!d.seek(frames+1),"out-of-range seek rejected");check(d.memoryMapStats().peakMappedBytes<=whic::MmapSampleStreamer::ViewBytes,"real source mapping remains within1MiB");
 }
 std::cout<<"Real PCM FNV1a64="<<std::hex<<decodedHash<<std::dec<<" frames="<<frames<<'\n';
 auto dir=fs::absolute("mmap-test-output")/("run-"+std::to_string(GetCurrentProcessId()));fs::create_directories(dir);auto large=dir/"REAL_RECORDING_REPEATED_TEST_ONLY.wav";
 unsigned repetitions=unsigned((3*1024*1024)/length+2);uint32_t dataBytes=uint32_t(length*repetitions);std::array<unsigned char,44> header{};memcpy(header.data(),"RIFF",4);memcpy(header.data()+8,"WAVEfmt ",8);memcpy(header.data()+36,"data",4);auto put16=[&](int p,unsigned v){header[p]=v&255;header[p+1]=(v>>8)&255;};auto put32=[&](int p,uint32_t v){for(int j=0;j<4;++j)header[p+j]=(v>>(j*8))&255;};put32(4,dataBytes+36);put32(16,16);put16(20,1);put16(22,channels);put32(24,rate);put32(28,rate*channels*2);put16(32,channels*2);put16(34,16);put32(40,dataBytes);
 {std::ofstream f(large,std::ios::binary);f.write((char*)header.data(),44);for(unsigned j=0;j<repetitions;++j)f.write((char*)bytes.data()+data,length);}
 whic::Sample big;big.path=large.wstring();check(whic::probeSample(big,error),"larger-than-view real-recording test copy probes");
 DWORD baseline=0;GetProcessHandleCount(GetCurrentProcess(),&baseline);
 {whic::Decoder d;check(d.open(big,error),"large source mapped decoder opens");std::array<float,10006> out{};uint64_t done=0;bool equal=true;while(size_t got=d.read(out.data(),5003)){for(size_t j=0;j<got;++j)for(int ch=0;ch<2;++ch)equal&=out[j*2+ch]==reference(done+j,ch);done+=got;}check(equal&&done==big.frames,"all bytes beyond multiple mapping windows match real PCM");auto stats=d.memoryMapStats();check(stats.mapCount>=3&&stats.peakMappedBytes<=1048576,"multiple bounded1MiB windows used");check(stats.pageRefills>2&&whic::MmapSampleStreamer::DecodedBytes==131072,"double-buffer decoded storage fixed128KiB");
  uint64_t boundary=(1048576-44)/(channels*2);for(uint64_t at:{boundary-3,boundary+3,big.frames-9,uint64_t(5)}){bool ok=d.seek(at);size_t got=d.read(out.data(),9);for(size_t j=0;j<got;++j)for(int ch=0;ch<2;++ch)ok&=out[j*2+ch]==reference(at+j,ch);check(ok&&got==9,"large mapped seek or view boundary matches source");}
  HANDLE writer=CreateFileW(large.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);check(writer==INVALID_HANDLE_VALUE,"active mapping prevents concurrent source mutation");if(writer!=INVALID_HANDLE_VALUE)CloseHandle(writer);
 }
 DWORD after=0;GetProcessHandleCount(GetCurrentProcess(),&after);check(after==baseline,"mapping file and section handles released");HANDLE writer=CreateFileW(large.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);check(writer!=INVALID_HANDLE_VALUE,"exclusive access restored after mapping destruction");if(writer!=INVALID_HANDLE_VALUE)CloseHandle(writer);
 auto truncated=dir/"TRUNCATED_TEST_ONLY.wav";{std::ofstream f(truncated,std::ios::binary);f.write((char*)bytes.data(),44);}whic::Sample bad;bad.path=truncated.wstring();check(!whic::probeSample(bad,error),"truncated real-container copy rejected before mapping");
 {whic::MmapSampleStreamer raw;check(!raw.open(large.wstring(),{44,big.frames+100,channels,16,false,false,false},error),"direct streamer rejects data outside file bounds");check(!raw.isOpen(),"failed mapping remains closed");}
 std::ifstream reread(source,std::ios::binary);std::vector<unsigned char> afterBytes((std::istreambuf_iterator<char>(reread)),{});check(bytes==afterBytes,"original real guitar bytes preserved");std::cout<<checks<<" checks; "<<failures<<" failures. No audio devices opened or audible output produced.\n";return failures?1:0;
}
