// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "sample.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <regex>
#include <algorithm>
#include <cmath>
#include <array>
#include <cstring>
#include <limits>
#include <atomic>
namespace whic {
namespace {
uint16_t u16(const unsigned char* p,bool be=false){return be?(p[0]<<8|p[1]):(p[1]<<8|p[0]);}
uint32_t u32(const unsigned char* p,bool be=false){return be?uint32_t(p[0])<<24|uint32_t(p[1])<<16|p[2]<<8|p[3]:uint32_t(p[3])<<24|uint32_t(p[2])<<16|p[1]<<8|p[0];}
bool tag(const unsigned char*p,const char*s){return !memcmp(p,s,4);}
std::string utf8(const std::wstring&s){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string o(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),o.data(),n,nullptr,nullptr);return o;}
std::wstring wide(const std::string&s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);std::wstring o(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),o.data(),n);return o;}
bool valid(const Mapping&m){return m.root>=0&&m.root<=127&&m.low>=0&&m.high<=127&&m.low<=m.high&&m.velLow>=1&&m.velHigh<=127&&m.velLow<=m.velHigh&&m.rr>=0&&m.rr<=127;}
bool flacInfo(const std::wstring&path,unsigned&rate,unsigned&channels,uint64_t&frames,std::wstring&err){std::ifstream f(std::filesystem::path(path),std::ios::binary);std::array<unsigned char,42>b{};if(!f.read((char*)b.data(),b.size())||!tag(b.data(),"fLaC")||(b[4]&127)!=0||b[5]!=0||b[6]!=0||b[7]!=34){err=L"Invalid FLAC STREAMINFO.";return false;}uint64_t packed=(uint64_t(u32(b.data()+18,true))<<32)|u32(b.data()+22,true);rate=unsigned(packed>>44);channels=unsigned((packed>>41)&7)+1;frames=packed&0xfffffffffULL;if(rate<8000||rate>384000||channels>2||!frames){err=L"FLAC must have a known frame count and mono/stereo 8–384 kHz audio.";return false;}return true;}
struct Native { std::ifstream f; uint64_t offset=0,bytes=0,frames=0,pos=0; unsigned rate=0,channels=0,bits=0,align=0; bool be=false,fp=false,unsigned8=false; };
bool parse(Native&n,const std::wstring&path,std::wstring&err){
 n.f.open(std::filesystem::path(path),std::ios::binary);if(!n.f){err=L"Cannot open sample file.";return false;}
 n.f.seekg(0,std::ios::end);auto end=n.f.tellg();if(end<12){err=L"Truncated audio file.";return false;}uint64_t size=uint64_t(end);n.f.seekg(0);unsigned char h[12];n.f.read((char*)h,12);
 bool wav=tag(h,"RIFF")&&tag(h+8,"WAVE"),aiff=tag(h,"FORM")&&(tag(h+8,"AIFF")||tag(h+8,"AIFC"));bool aifc=aiff&&tag(h+8,"AIFC");if(!wav&&!aiff){err=L"Unsupported audio container.";return false;}
 n.be=aiff;bool fmt=false,data=false;uint64_t declared=uint64_t(u32(h+4,aiff))+8;if(declared>size||declared<12){err=L"Invalid container length.";return false;}size=declared;
 for(uint64_t p=12;p+8<=size;){unsigned char ch[8];n.f.seekg(p);n.f.read((char*)ch,8);uint32_t len=u32(ch+4,aiff);uint64_t start=p+8;if(len>size-start){err=L"Audio chunk exceeds file bounds.";return false;}
 if((wav&&tag(ch,"fmt "))||(aiff&&tag(ch,"COMM"))){std::array<unsigned char,64>b{};if(len<(wav?16u:18u)){err=L"Truncated sample format.";return false;}n.f.read((char*)b.data(),std::min<size_t>(len,b.size()));
 if(wav){unsigned format=u16(b.data());n.channels=u16(b.data()+2);n.rate=u32(b.data()+4);n.align=u16(b.data()+12);n.bits=u16(b.data()+14);if(format==65534){if(len<40||u16(b.data()+16)<22){err=L"Invalid extensible WAV.";return false;}format=u16(b.data()+24);}n.fp=format==3;n.unsigned8=format==1;if(format!=1&&format!=3){err=L"WAV must contain PCM or IEEE float.";return false;}}
 else {n.channels=u16(b.data(),true);n.bits=u16(b.data()+6,true);unsigned exp=u16(b.data()+8,true);uint64_t mant=(uint64_t(u32(b.data()+10,true))<<32)|u32(b.data()+14,true);double rate=std::ldexp(double(mant),int(exp&32767)-16383-63);if((exp&32768)||!std::isfinite(rate)||rate<8000||rate>384000){err=L"Invalid AIFF rate.";return false;}n.rate=unsigned(std::llround(rate));if(aifc){if(len<22){err=L"Truncated AIFC format.";return false;}auto c=b.data()+18;if(tag(c,"sowt"))n.be=false;else if(tag(c,"fl32")||tag(c,"FL32")){n.fp=true;n.bits=32;}else if(!tag(c,"NONE")&&!tag(c,"twos")){err=L"Compressed AIFC encoding is unsupported.";return false;}}n.align=n.channels*((n.bits+7)/8);}
 fmt=true;
 } else if(wav&&tag(ch,"data")){n.offset=start;n.bytes=len;data=true;}else if(aiff&&tag(ch,"SSND")){if(len<8){err=L"Truncated AIFF data.";return false;}unsigned char b[8];n.f.read((char*)b,8);uint32_t skip=u32(b,true);if(skip>len-8){err=L"Invalid AIFF offset.";return false;}n.offset=start+8+skip;n.bytes=len-8-skip;data=true;}
 p=start+len+(len&1);
 }
 if(!fmt||!data||n.channels<1||n.channels>2||n.rate<8000||n.rate>384000||(n.bits!=8&&n.bits!=16&&n.bits!=24&&n.bits!=32)||(n.fp&&n.bits!=32)||n.align!=n.channels*(n.bits/8)||n.bytes%n.align){err=L"Unsupported or malformed audio: use mono/stereo PCM 8/16/24/32-bit or float32.";return false;}
 n.frames=n.bytes/n.align;if(!n.frames){err=L"Sample contains no frames.";return false;}n.f.clear();n.f.seekg(n.offset);return true;
}
float pcm(const unsigned char*p,const Native&n){if(n.fp){uint32_t v=u32(p,n.be);float f;memcpy(&f,&v,4);return std::isfinite(f)?std::clamp(f,-1.f,1.f):0.f;}int64_t v=0;unsigned bytes=n.bits/8;for(unsigned i=0;i<bytes;++i)v|=int64_t(p[n.be?bytes-1-i:i])<<(i*8);if(n.bits==8&&n.unsigned8)v-=128;else if(v&(int64_t(1)<<(n.bits-1)))v-=int64_t(1)<<n.bits;return float(double(v)/double(int64_t(1)<<(n.bits-1)));}
}
namespace { std::atomic<uint64_t> cacheBytes{0}; constexpr uint64_t MaxCacheBytes=1024ULL*1024*1024,MaxSampleCacheBytes=256ULL*1024*1024; }
struct PreparedAudio {
 std::wstring path,directory,source; uint64_t reserved=0,frames=0;uint32_t rate=0;HANDLE writer=INVALID_HANDLE_VALUE;
 ~PreparedAudio(){if(writer!=INVALID_HANDLE_VALUE)CloseHandle(writer);if(!path.empty())DeleteFileW(path.c_str());if(!directory.empty())RemoveDirectoryW(directory.c_str());if(reserved)cacheBytes.fetch_sub(reserved,std::memory_order_relaxed);}
};
struct Decoder::Impl {std::shared_ptr<PreparedAudio> prepared;Native n; MmapSampleStreamer mapped; IMFSourceReader*r=nullptr; bool com=false,mf=false;unsigned rate=0,channels=0;uint64_t frames=0;std::vector<float> pending;size_t cursor=0;uint64_t streamPos=0;bool eof=false;std::wstring path;~Impl(){if(r)r->Release();if(mf)MFShutdown();if(com)CoUninitialize();}};
Decoder::Decoder():impl(std::make_unique<Impl>()){} Decoder::~Decoder()=default;
bool Decoder::open(const Sample&s,std::wstring&err){impl=std::make_unique<Impl>();auto&i=*impl;i.path=s.path;auto ext=std::filesystem::path(s.path).extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),towlower);
 if(s.prepared){if(s.prepared->source!=s.path||s.prepared->frames!=s.frames||s.prepared->rate!=s.rate){err=L"Prepared sample metadata changed; import the source again.";return false;}i.prepared=s.prepared;if(!parse(i.n,s.prepared->path,err))return false;i.rate=i.n.rate;i.frames=i.n.frames;i.channels=i.n.channels;i.n.f.close();return i.mapped.open(s.prepared?s.prepared->path:s.path,{i.n.offset,i.n.frames,i.n.channels,i.n.bits,i.n.be,i.n.fp,i.n.unsigned8},err);}
 if(ext!=L".flac"){if(!parse(i.n,s.path,err))return false;i.rate=i.n.rate;i.frames=i.n.frames;i.channels=i.n.channels;i.n.f.close();return i.mapped.open(s.prepared?s.prepared->path:s.path,{i.n.offset,i.n.frames,i.n.channels,i.n.bits,i.n.be,i.n.fp,i.n.unsigned8},err);}
 HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);i.com=SUCCEEDED(hr);if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE){err=L"COM audio initialization failed.";return false;}hr=MFStartup(MF_VERSION);if(FAILED(hr)){err=L"Windows Media Foundation unavailable.";return false;}i.mf=true;
 hr=MFCreateSourceReaderFromURL(s.path.c_str(),nullptr,&i.r);if(FAILED(hr)){err=L"Windows FLAC decoder could not open this sample.";return false;}i.r->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);i.r->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE);
 IMFMediaType*t=nullptr;MFCreateMediaType(&t);if(!t){err=L"Cannot allocate decoder format.";return false;}t->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);t->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_Float);hr=i.r->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,t);t->Release();if(FAILED(hr)){err=L"FLAC float decode negotiation failed.";return false;}
 hr=i.r->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,&t);if(FAILED(hr)){err=L"Cannot read decoded format.";return false;}UINT32 rate=0,ch=0;t->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&rate);t->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&ch);t->Release();if(ch<1||ch>2||rate<8000||rate>384000){err=L"FLAC must be mono/stereo at 8–384 kHz.";return false;}i.rate=rate;i.channels=ch;unsigned headerRate=0,headerChannels=0;if(!flacInfo(s.path,headerRate,headerChannels,i.frames,err))return false;if(headerRate!=rate||headerChannels!=ch){err=L"FLAC header and decoded format disagree.";return false;}return true;
}
bool Decoder::usingMemoryMap() const {return impl->mapped.isOpen();}
MmapSampleStreamer::Stats Decoder::memoryMapStats() const {return impl->mapped.stats();}
bool Decoder::usingPreparedPcm() const {return bool(impl->prepared)&&impl->r==nullptr;}
bool isPlaybackPrepared(const Sample&s){auto ext=std::filesystem::path(s.path).extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),towlower);return ext!=L".flac"||(s.prepared&&s.prepared->source==s.path&&s.prepared->frames==s.frames&&s.prepared->rate==s.rate);}
bool prepareForPlayback(Sample&s,std::wstring&error){
 if(isPlaybackPrepared(s))return true;
 // The source remains the preset identity. The cache is a temporary decoded copy,
 // never a generated instrument, and is retained by every active decoder.
 s.prepared.reset();if(!probeSample(s,error))return false;
 if(s.frames>(MaxSampleCacheBytes-44)/8){error=L"This FLAC needs more than the 256 MiB per-sample PCM cache limit. Use a shorter source or WAV/AIFF.";return false;}
 auto cache=std::make_shared<PreparedAudio>();cache->frames=s.frames;cache->rate=s.rate;cache->source=s.path;
 uint64_t needed=s.frames*8+44,used=cacheBytes.load(std::memory_order_relaxed);
 do{if(used>MaxCacheBytes-needed){error=L"FLAC PCM cache would exceed the 1 GiB session limit. Remove some instruments or use WAV/AIFF.";return false;}}while(!cacheBytes.compare_exchange_weak(used,used+needed,std::memory_order_relaxed));cache->reserved=needed;
 wchar_t temp[32768];DWORD length=GetTempPathW(32768,temp);if(!length||length>=32768){error=L"Cannot locate the Windows temporary directory.";return false;}
 GUID id{};wchar_t guid[40];if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,guid,40)){error=L"Cannot create a unique PCM cache identity.";return false;}
 std::wstring dir=std::wstring(temp)+L"WHIC-PCM-"+guid;
 if(!CreateDirectoryW(dir.c_str(),nullptr)){error=L"Cannot create a private PCM cache directory.";return false;}cache->directory=dir;
 std::wstring path=dir+L"\\decoded.wav";cache->writer=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
 if(cache->writer==INVALID_HANDLE_VALUE){error=L"Cannot create the PCM cache file.";return false;}cache->path=path;
 auto write=[&](const void*data,DWORD bytes){DWORD written=0;return WriteFile(cache->writer,data,bytes,&written,nullptr)&&written==bytes;};
 std::array<unsigned char,44> header{};memcpy(header.data(),"RIFF",4);memcpy(header.data()+8,"WAVEfmt ",8);memcpy(header.data()+36,"data",4);
 auto put16=[&](int off,uint16_t v){header[off]=static_cast<unsigned char>(v);header[off+1]=static_cast<unsigned char>(v>>8);};
 auto put32=[&](int off,uint32_t v){for(int j=0;j<4;++j)header[off+j]=static_cast<unsigned char>(v>>(j*8));};
 put32(4,uint32_t(needed-8));put32(16,16);put16(20,3);put16(22,2);put32(24,s.rate);put32(28,s.rate*8);put16(32,8);put16(34,32);put32(40,uint32_t(s.frames*8));
 if(!write(header.data(),DWORD(header.size()))){error=L"Failed to write PCM cache header (check free disk space).";return false;}
 Sample source=s;source.prepared.reset();Decoder decoder;if(!decoder.open(source,error))return false;
 std::array<float,16384> chunk{};uint64_t remaining=s.frames;
 while(remaining){size_t count=size_t(std::min<uint64_t>(8192,remaining));if(decoder.read(chunk.data(),count)!=count){error=L"FLAC decoded fewer frames than declared; no playable cache was created.";return false;}if(!write(chunk.data(),DWORD(count*8))){error=L"PCM cache disk write failed (check free disk space).";return false;}remaining-=count;}
 // An extra frame indicates malformed metadata; never silently truncate it.
 if(decoder.read(chunk.data(),1)!=0){error=L"FLAC decoded more frames than its header declared.";return false;}
 if(!FlushFileBuffers(cache->writer)){error=L"Could not flush the PCM cache to disk.";return false;}CloseHandle(cache->writer);cache->writer=INVALID_HANDLE_VALUE;
 Native check;if(!parse(check,cache->path,error)||check.frames!=s.frames||check.rate!=s.rate){error=L"Prepared PCM cache validation failed.";return false;}
 s.prepared=std::move(cache);return true;
}
bool Decoder::seek(uint64_t frame){auto&i=*impl;if(frame>i.frames)return false;if(!i.r)return i.mapped.seek(frame);// Windows FLAC handlers may report seek success but fail after EOS. Reopen
// on backward seeks and decode/discard forward; normal playback reads sequentially.
if(frame<i.streamPos){Sample s;s.path=i.path;std::wstring error;if(!open(s,error))return false;}
std::array<float,8192> scratch{};while(impl->streamPos<frame){size_t take=size_t(std::min<uint64_t>(4096,frame-impl->streamPos));if(read(scratch.data(),take)!=take)return false;}return true;}
size_t Decoder::read(float*out,size_t count){auto&i=*impl;size_t done=0;if(!i.r)return i.mapped.read(out,count);
 unsigned empty=0;while(done<count){if(i.cursor<i.pending.size()){size_t k=std::min(count-done,(i.pending.size()-i.cursor)/i.channels);for(size_t j=0;j<k;++j){float l=i.pending[i.cursor++],r=i.channels==2?i.pending[i.cursor++]:l;out[done*2]=std::isfinite(l)?std::clamp(l,-1.f,1.f):0;out[done*2+1]=std::isfinite(r)?std::clamp(r,-1.f,1.f):0;++done;}continue;}if(i.eof)break;IMFSample*s=nullptr;DWORD flags=0;LONGLONG ts=0;HRESULT hr=i.r->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&flags,&ts,&s);if(flags&MF_SOURCE_READERF_ENDOFSTREAM)i.eof=true;if(FAILED(hr)||flags&MF_SOURCE_READERF_ERROR){i.eof=true;if(s)s->Release();break;}if(!s){if(flags&MF_SOURCE_READERF_ENDOFSTREAM||++empty>16)break;continue;}IMFMediaBuffer*b=nullptr;hr=s->ConvertToContiguousBuffer(&b);s->Release();if(FAILED(hr))break;BYTE*p=nullptr;DWORD len=0;hr=b->Lock(&p,nullptr,&len);if(FAILED(hr)){b->Release();break;}if(len>1024*1024||len%(sizeof(float)*i.channels)){b->Unlock();b->Release();break;}i.pending.resize(len/4);memcpy(i.pending.data(),p,len);i.cursor=0;b->Unlock();b->Release();if(!len&&++empty>16)break;}i.streamPos+=done;return done;
}
bool probeSample(Sample&s,std::wstring&error){auto ext=std::filesystem::path(s.path).extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),towlower);if(ext!=L".flac"){Native n;if(!parse(n,s.path,error))return false;s.rate=n.rate;s.frames=n.frames;return true;}Decoder d;if(!d.open(s,error))return false;
 unsigned channels=0;return flacInfo(s.path,s.rate,channels,s.frames,error);
}
Mapping inferMapping(const std::wstring&path){Mapping m;auto name=std::filesystem::path(path).stem().wstring();std::wsmatch hit;if(std::regex_search(name,hit,std::wregex(L"(?:^|[_ \\-])([A-Ga-g])([#b]?)(-?[0-9])(?:$|[_ \\-])"))){int semis[]={9,11,0,2,4,5,7};int note=semis[towupper(hit[1].str()[0])-L'A'];if(hit[2]==L"#")++note;if(hit[2]==L"b")--note;int n=(std::stoi(hit[3])+1)*12+note;if(n>=0&&n<=127)m.root=n;}
 if(std::regex_search(name,hit,std::wregex(L"(?:^|[_ \\-])(?:key|note)([0-9]{1,3})(?:$|[_ \\-])",std::regex::icase)))m.root=std::clamp(std::stoi(hit[1]),0,127);
 if(std::regex_search(name,hit,std::wregex(L"(?:^|[_ \\-])v(?:el)?([0-9]{1,3})[-_]([0-9]{1,3})(?:$|[_ \\-])",std::regex::icase))){m.velLow=std::clamp(std::stoi(hit[1]),1,127);m.velHigh=std::clamp(std::stoi(hit[2]),m.velLow,127);}
 if(std::regex_search(name,hit,std::wregex(L"(?:^|[_ \\-])rr([0-9]{1,3})(?:$|[_ \\-])",std::regex::icase)))m.rr=std::clamp(std::stoi(hit[1]),0,127);m.low=m.high=m.root;return m;}
bool validSampleParameters(const Sample& s){return (!s.frames ? s.startOffset==0 : s.startOffset<s.frames)&&std::isfinite(s.tuneCents)&&s.tuneCents>=-1200.f&&s.tuneCents<=1200.f&&std::isfinite(s.gainDb)&&s.gainDb>=-96.f&&s.gainDb<=60.f&&std::isfinite(s.attackSeconds)&&s.attackSeconds>=0.f&&s.attackSeconds<=30.f;}
bool savePreset(const std::wstring&path,const std::vector<Sample>&samples,const std::wstring&provenance,std::wstring&error,const DspParameters& dsp){
 if(samples.size()>4096||provenance.size()>1048576){error=L"Preset exceeds zone or provenance limit.";return false;}
 std::error_code ec;auto target=std::filesystem::absolute(std::filesystem::path(path),ec).lexically_normal();if(_wcsicmp(target.extension().c_str(),L".whic")!=0){error=L"Preset filename must end in .whic.";return false;}if(ec){error=L"Invalid preset destination.";return false;}
 for(auto&s:samples){if(!validSampleParameters(s)||!valid(s.map)||!std::isfinite(s.releaseSeconds)||s.releaseSeconds<0.001f||s.releaseSeconds>30.f||s.path.empty()||s.path.find(L'\0')!=std::wstring::npos||s.path.size()>32767){error=L"Invalid sample mapping or path.";return false;}auto source=std::filesystem::absolute(std::filesystem::path(s.path),ec).lexically_normal();if(ec){error=L"Cannot resolve source sample path.";return false;}if((_wcsicmp(target.c_str(),source.c_str())==0||std::filesystem::equivalent(target,source,ec))){error=L"A preset cannot overwrite a source sample.";return false;}ec.clear();}
 auto temp=target.wstring()+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
 std::ofstream o(std::filesystem::path(temp),std::ios::binary);if(!o){error=L"Cannot write preset.";return false;}
 auto params=clampDspParameters(dsp);o<<"WHIC_PRESET 6\n"<<std::setprecision(std::numeric_limits<float>::max_digits10)<<std::quoted(utf8(provenance))<<'\n'<<params.resonance<<' '<<params.tension<<' '<<params.strike<<' '<<params.friction<<' '<<params.hardness<<' '<<params.strikeWindowMs<<' '<<params.detuneCents<<' '<<params.stiffness<<' '<<params.decaySeconds<<' '<<params.sympathetic<<' '<<params.releaseNoise<<' '<<params.blend<<' '<<int(params.physicsOn)<<' '<<int(params.legacyMode)<<'\n'<<samples.size()<<'\n';for(auto&s:samples){auto&m=s.map;auto source=std::filesystem::absolute(std::filesystem::path(s.path),ec).lexically_normal();auto stored=source;auto relative=source.lexically_relative(target.parent_path());unsigned parents=0;for(const auto& part:relative){if(part==L"..")++parents;else break;}if(!ec&&!relative.empty()&&parents<=1)stored=relative;o<<std::quoted(utf8(stored.wstring()))<<' '<<m.root<<' '<<m.low<<' '<<m.high<<' '<<m.velLow<<' '<<m.velHigh<<' '<<m.rr<<' '<<std::setprecision(std::numeric_limits<float>::max_digits10)<<s.releaseSeconds<<' '<<s.startOffset<<' '<<s.tuneCents<<' '<<s.gainDb<<' '<<s.attackSeconds<<' '<<int(s.releaseTrigger)<<'\n';}
 o.flush();bool ok=bool(o);o.close();if(!ok||!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp.c_str());error=L"Preset write or atomic replacement failed.";return false;}return true;
}
bool loadPreset(const std::wstring&path,std::vector<Sample>&samples,std::wstring&provenance,std::wstring&error,DspParameters* dsp){std::error_code pathError;auto presetPath=std::filesystem::absolute(std::filesystem::path(path),pathError).lexically_normal();if(pathError){error=L"Invalid preset path.";return false;}std::ifstream f(presetPath,std::ios::binary);if(!f){error=L"Cannot open preset.";return false;}f.seekg(0,std::ios::end);if(f.tellg()>16*1024*1024){error=L"Preset exceeds 16 MiB limit.";return false;}f.seekg(0);std::string magic,p;int version=0;size_t count=0;if(!(f>>magic>>version)||magic!="WHIC_PRESET"||(version!=1&&version!=2&&version!=3&&version!=4&&version!=5&&version!=6)||!(f>>std::quoted(p))){error=L"Invalid or unsupported preset.";return false;}DspParameters loadedDsp{};if(version>=3){if(!(f>>loadedDsp.resonance>>loadedDsp.tension>>loadedDsp.strike>>loadedDsp.friction)){error=L"Invalid preset sample-processing parameters.";return false;}if(version>=5){int enabled=0;if(!(f>>loadedDsp.hardness>>loadedDsp.strikeWindowMs>>loadedDsp.detuneCents>>loadedDsp.stiffness>>loadedDsp.decaySeconds>>loadedDsp.sympathetic>>loadedDsp.releaseNoise>>loadedDsp.blend>>enabled)||(enabled!=0&&enabled!=1)){error=L"Invalid physics parameters";return false;}loadedDsp.physicsOn=enabled!=0;}if(version<5)loadedDsp.legacyMode=true;if(version>=6){int legacy=0;if(!(f>>legacy)||(legacy!=0&&legacy!=1)){error=L"Invalid legacy mode";return false;}loadedDsp.legacyMode=legacy!=0;}loadedDsp=clampDspParameters(loadedDsp);}if(!(f>>count)||count>4096){error=L"Invalid preset zone count.";return false;}std::vector<Sample> loaded;loaded.reserve(count);for(size_t j=0;j<count;++j){Sample s;std::string path8;auto&m=s.map;if(!(f>>std::quoted(path8)>>m.root>>m.low>>m.high>>m.velLow>>m.velHigh>>m.rr)||!valid(m)){error=L"Invalid preset zone.";return false;}if(version>=2&&(!(f>>s.releaseSeconds)||!std::isfinite(s.releaseSeconds)||s.releaseSeconds<0.001f||s.releaseSeconds>30.f)){error=L"Invalid preset release time (expected 0.001 to 30 seconds).";return false;}if(version>=4&&(!(f>>s.startOffset>>s.tuneCents>>s.gainDb>>s.attackSeconds)||!std::isfinite(s.tuneCents)||!std::isfinite(s.gainDb)||!std::isfinite(s.attackSeconds))){error=L"Invalid preset source parameters.";return false;}if(version>=5){int trigger=0;if(!(f>>trigger)||(trigger!=0&&trigger!=1)){error=L"Invalid sample trigger";return false;}s.releaseTrigger=trigger!=0;}s.path=wide(path8);std::filesystem::path sourcePath(s.path);if(s.path.empty()||s.path.find(L'\0')!=std::wstring::npos||(sourcePath.is_relative()&&(sourcePath.has_root_name()||sourcePath.has_root_directory()))){error=L"Invalid preset sample path.";return false;}if(sourcePath.is_relative())sourcePath=presetPath.parent_path()/sourcePath;s.path=sourcePath.lexically_normal().wstring();if(!probeSample(s,error)){error=L"Cannot load sample: "+s.path+L". "+error;return false;}if(!validSampleParameters(s)){error=L"Preset source offset/tuning/gain/attack outside supported range.";return false;}loaded.push_back(std::move(s));}samples=std::move(loaded);provenance=wide(p);if(dsp)*dsp=loadedDsp;return true;}
}















