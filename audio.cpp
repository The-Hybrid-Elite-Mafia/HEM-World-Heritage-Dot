// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "audio.h"
#include "legacy_dsp.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <mmreg.h>
#include <mmsystem.h>
#include <ks.h>
#include <ksmedia.h>
#include <array>
#include <atomic>
#include <thread>
#include <mutex>
#include <future>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <chrono>
#include <cstring>
namespace whic {
namespace {
constexpr unsigned Voices=32, RingFrames=wasapi_policy::residentRingFrames, Ahead=1024, Chunk=2048;
static_assert((RingFrames & (RingFrames-1))==0, "Ring indexing must remain continuous when unsigned counters wrap");
static_assert(std::atomic<float>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free && std::atomic<unsigned long long>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
std::wstring hrText(const wchar_t* message,HRESULT hr) {std::wostringstream s;s<<message<<L" (0x"<<std::hex<<static_cast<unsigned long>(hr)<<L")";return s.str();}
}
namespace {
bool pcmFormat(const WAVEFORMATEX* f,bool& floating){
 floating=f->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;bool pcm=f->wFormatTag==WAVE_FORMAT_PCM;
 if(f->wFormatTag==WAVE_FORMAT_EXTENSIBLE&&f->cbSize>=22){auto* e=reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);floating=IsEqualGUID(e->SubFormat,KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);pcm=IsEqualGUID(e->SubFormat,KSDATAFORMAT_SUBTYPE_PCM);if(e->Samples.wValidBitsPerSample!=f->wBitsPerSample)return false;}
 return (floating||pcm)&&wasapi_policy::supportedPcm(f->nSamplesPerSec,f->nChannels,f->wBitsPerSample,floating)&&f->nBlockAlign==f->nChannels*(f->wBitsPerSample/8);
}
HRESULT chooseFormat(IAudioClient* client,WasapiMode mode,WAVEFORMATEX** chosen){
 *chosen=nullptr;WAVEFORMATEX* mix=nullptr;HRESULT hr=client->GetMixFormat(&mix);if(FAILED(hr))return hr;bool floating=false;
 auto share=mode==WasapiMode::Shared?AUDCLNT_SHAREMODE_SHARED:AUDCLNT_SHAREMODE_EXCLUSIVE;
 WAVEFORMATEX* closest=nullptr;hr=pcmFormat(mix,floating)?client->IsFormatSupported(share,mix,mode==WasapiMode::Shared?&closest:nullptr):AUDCLNT_E_UNSUPPORTED_FORMAT;if(closest)CoTaskMemFree(closest);
 if(hr==S_OK&&mix->nSamplesPerSec==48000){*chosen=mix;return S_OK;}if(mode==WasapiMode::Shared){CoTaskMemFree(mix);return AUDCLNT_E_UNSUPPORTED_FORMAT;}
 unsigned channels=mix->nChannels==1?1:2;std::array<DWORD,1> rates{48000};CoTaskMemFree(mix);
 for(DWORD hz:rates)for(unsigned kind=0;kind<4;++kind)for(unsigned extended=0;extended<2;++extended){
  unsigned bits=kind==0?32:kind==1?16:kind==2?24:32;WAVEFORMATEXTENSIBLE candidate{};auto& f=candidate.Format;f.wFormatTag=extended?WAVE_FORMAT_EXTENSIBLE:kind==0?WAVE_FORMAT_IEEE_FLOAT:WAVE_FORMAT_PCM;f.nChannels=WORD(channels);f.nSamplesPerSec=hz;f.wBitsPerSample=WORD(bits);f.nBlockAlign=WORD(channels*bits/8);f.nAvgBytesPerSec=hz*f.nBlockAlign;f.cbSize=extended?22:0;candidate.Samples.wValidBitsPerSample=WORD(bits);candidate.dwChannelMask=channels==1?SPEAKER_FRONT_CENTER:SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT;candidate.SubFormat=kind==0?KSDATAFORMAT_SUBTYPE_IEEE_FLOAT:KSDATAFORMAT_SUBTYPE_PCM;
  if(!pcmFormat(&f,floating))continue;hr=client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,&f,nullptr);if(hr==S_OK){auto* copy=static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(candidate)));if(!copy)return E_OUTOFMEMORY;memcpy(copy,&candidate,sizeof(candidate));*chosen=copy;return S_OK;}
 }
 return AUDCLNT_E_UNSUPPORTED_FORMAT;
}
const wchar_t* initializeReason(HRESULT hr){if(hr==AUDCLNT_E_DEVICE_IN_USE)return L"Output device is busy; exclusive mode was not granted";if(hr==AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED)return L"Exclusive mode is disabled for this output device";if(hr==AUDCLNT_E_UNSUPPORTED_FORMAT)return L"Output format is unsupported; no silent mode fallback was attempted";return L"WASAPI initialization failed";}
struct ProbeSession {HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);IMMDeviceEnumerator* enumeration=nullptr;IMMDevice* device=nullptr;IAudioClient* client=nullptr;~ProbeSession(){if(client)client->Release();if(device)device->Release();if(enumeration)enumeration->Release();if(SUCCEEDED(com))CoUninitialize();}
 HRESULT open(const std::wstring& id){HRESULT h=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumeration));if(SUCCEEDED(h))h=id.empty()?enumeration->GetDefaultAudioEndpoint(eRender,eConsole,&device):enumeration->GetDevice(id.c_str(),&device);if(SUCCEEDED(h))h=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&client));return h;}
};
}
struct AudioEngine::Impl {
 std::atomic<long> audioError{S_OK}; std::atomic<bool> running{false}; std::atomic<unsigned> rate{48000},active{0};
 std::atomic<unsigned long long> underruns{0},failures{0},steals{0};
 std::array<std::atomic<unsigned>,128> notes{}; std::array<unsigned,128> observed{},round{};
 whic_legacy::SampleDsp dsp; std::atomic<bool> legacyMode{false}; std::atomic<float> resonance{0},tension{0},strike{1},friction{0};
 // Deployed legacy handoff: one worker publishes processed PCM with write/release;
 // transfer reads write/acquire, then releases consumed frames through read.
 // Ring pages are pre-touched and VirtualLock-resident before threads start.
 // realtime_handoff.h ResidentDoubleBuffer is a separately tested future ASIO
 // adapter contract; it is not misrepresented as a deployed ASIO callback.
 float* ring=nullptr; bool ringLocked=false; static constexpr size_t RingBytes=RingFrames*2*sizeof(float); std::atomic<unsigned> write{0},read{0};
 AsioBackend asio;
 std::thread output,worker; HANDLE event=nullptr; HMIDIIN midi=nullptr;
 std::mutex libraryMutex,messageMutex; std::vector<Sample> samples; unsigned generation=0;
 std::wstring endpointId; WasapiMode mode=WasapiMode::Shared; std::atomic<unsigned> ahead{Ahead};
 std::wstring lastMessage=L"WASAPI stopped";
 struct Voice {std::unique_ptr<SampleDsp> physics;bool releaseSample=false,inTail=false;unsigned tailRemaining=0;std::unique_ptr<Decoder> decoder; std::array<float,(Chunk+1)*2> data{}; uint64_t base=0,total=0; size_t count=0; double position=0,step=1;float gain=0,envelope=0,releaseStep=1.f/2400,attackStep=1.f/144;int key=-1;bool release=false;};
 std::array<Voice,Voices> voices;
 std::atomic<bool> sustain{false},usingAsio{false};std::array<int,128> lastVelocity{};std::array<bool,128> deferred{};std::atomic<unsigned> deviceFrames{0};std::atomic<uint64_t> decodedBytes{0};std::atomic<float> peakL{0},peakR{0};std::array<std::atomic<float>,8> physics{{.5f,40,0,.25f,1.5f,.2f,.5f,0}};std::atomic<bool> physicsOn{true};
 DspParameters parameters(){DspParameters p{resonance.load(),tension.load(),strike.load(),friction.load()};p.hardness=physics[0];p.strikeWindowMs=physics[1];p.detuneCents=physics[2];p.stiffness=physics[3];p.decaySeconds=physics[4];p.sympathetic=physics[5];p.releaseNoise=physics[6];p.blend=physics[7];p.physicsOn=physicsOn;p.legacyMode=legacyMode;return p;}
 void message(const std::wstring& s){std::lock_guard lock(messageMutex);lastMessage=s;}
 static void CALLBACK midiCallback(HMIDIIN,UINT msg,DWORD_PTR self,DWORD_PTR packed,DWORD_PTR) {
  if(msg!=MIM_DATA)return;auto* p=reinterpret_cast<Impl*>(self);unsigned code=packed&0xf0,key=(packed>>8)&127,vel=(packed>>16)&127;
  if(code==0x90||code==0x80){unsigned old=p->notes[key].load(std::memory_order_relaxed);p->notes[key].store(((old+256)&0xffffff00u)|(code==0x90?vel:0),std::memory_order_release);}
  if(code==0xb0&&key==64)p->sustain=vel>=64;
  if(code==0xb0 && (key==120||key==123))for(auto& n:p->notes){unsigned old=n.load(std::memory_order_relaxed);n.store((old+256)&0xffffff00u,std::memory_order_release);}
 }
 int begin(int key,int velocity,bool releaseTrigger=false) {
  std::lock_guard lock(libraryMutex);
  const Sample* chosen=nullptr;unsigned eligible=0;
  // RR alternatives are sorted by their manually editable rr index, stable on ties.
  std::array<const Sample*,4096> candidates{};
  for(const auto& s:samples)if(s.releaseTrigger==releaseTrigger&&key>=s.map.low&&key<=s.map.high&&velocity>=s.map.velLow&&velocity<=s.map.velHigh){if(eligible<candidates.size())candidates[eligible++]=&s;}
  if(!eligible)return -1;
  std::sort(candidates.begin(),candidates.begin()+eligible,[](auto a,auto b){return a->map.rr<b->map.rr;});chosen=candidates[round[key]++%eligible];
  auto it=std::find_if(voices.begin(),voices.end(),[](auto& v){return !v.decoder;});
  if(it==voices.end()){it=std::min_element(voices.begin(),voices.end(),[](auto& a,auto& b){return a.envelope*a.gain<b.envelope*b.gain;});++steals;}
  auto& v=*it;v.decoder.reset();v=Voice{};v.decoder=std::make_unique<Decoder>();std::wstring error;
  if(!v.decoder->open(*chosen,error)){v.decoder.reset();++failures;message(error);return -1;}
  v.releaseSample=releaseTrigger;v.physics=std::make_unique<SampleDsp>();v.physics->reset(float(rate.load()));v.physics->setFundamental(440.f*std::exp2((key-69)/12.f));{auto vp=parameters();if(vp.legacyMode){vp.strike=1;vp.tension=0;vp.friction=0;}v.physics->setParameters(vp);}v.key=key;v.total=chosen->frames;v.base=chosen->startOffset;v.position=double(chosen->startOffset);v.step=double(chosen->rate)/rate.load()*std::pow(2.0,(key-chosen->map.root)/12.0)*std::pow(2.0,chosen->tuneCents/1200.0);v.gain=0.25f*velocity/127.0f*std::pow(10.f,chosen->gainDb/20.f);v.releaseStep=1.f/(chosen->releaseSeconds*rate.load());v.attackStep=1.f/std::max(1.f,chosen->attackSeconds*rate.load());if(!v.decoder->seek(chosen->startOffset)){v.decoder.reset();++failures;message(L"Cannot seek to mapped sample start offset");return -1;}
  v.count=v.decoder->read(v.data.data(),Chunk+1);decodedBytes+=v.count*8;if(releaseTrigger)v.gain*=physics[6].load();if(!v.count){v.decoder.reset();++failures;return -1;} return static_cast<int>(chosen-samples.data());
 }
 int applyNote(int key,int velocity){if(!velocity&&sustain){deferred[key]=true;return -1;}for(auto& v:voices)if(v.decoder&&v.key==key&&!v.releaseSample)v.release=true;if(velocity){deferred[key]=false;lastVelocity[key]=velocity;return begin(key,velocity);}int previous=lastVelocity[key];lastVelocity[key]=0;deferred[key]=false;return previous&&physics[6].load()>0?begin(key,previous,true):-1;}
 void render(float* target,unsigned count) {
  std::fill(target,target+count*2,0.f);unsigned live=0;
  for(auto& v:voices){if(!v.decoder)continue;++live;{auto vp=parameters();if(vp.legacyMode){vp.strike=1;vp.tension=0;vp.friction=0;}v.physics->setParameters(vp);}
   for(unsigned i=0;i<count;++i){
    const uint64_t frame=static_cast<uint64_t>(v.position);
    if(frame>=v.total||(v.release&&v.envelope<=0)){auto params=parameters();if(!params.physicsOn||params.blend<=0){v.decoder.reset();break;}if(!v.inTail){v.inTail=true;v.tailRemaining=unsigned(params.decaySeconds*rate.load());}if(!v.tailRemaining--){v.decoder.reset();break;}float tail[2]{};v.physics->process(tail,1);target[i*2]+=tail[0];target[i*2+1]+=tail[1];continue;}
    if(frame<v.base || frame+1>=v.base+v.count){
     // Preserve the boundary frame and advance sequentially for normal playback.
     // This avoids repeated codec seeks and timestamp rounding at cache boundaries.
     if(frame>=v.base && frame<v.base+v.count+Chunk && v.count>1){
      size_t last=v.count-1;float left=v.data[last*2],right=v.data[last*2+1];v.base+=last;
      v.data[0]=left;v.data[1]=right;v.count=1+v.decoder->read(v.data.data()+2,Chunk);decodedBytes+=(v.count-1)*8;
     } else {
      // Only extreme transpositions need a jump. This is the background worker.
      v.base=frame;if(!v.decoder->seek(frame)){v.decoder.reset();++failures;break;}
      v.count=v.decoder->read(v.data.data(),Chunk+1);decodedBytes+=v.count*8;
     }
     if(!v.count || frame>=v.base+v.count){v.decoder.reset();++failures;break;}
    }
    size_t a=static_cast<size_t>(frame-v.base),b=std::min(a+1,v.count-1);float fraction=float(v.position-frame);
    if(v.release)v.envelope=std::max(0.f,v.envelope-v.releaseStep);else v.envelope=std::min(1.f,v.envelope+v.attackStep);
    if(v.release && v.envelope<=0)continue;
    float framePcm[2];for(unsigned ch=0;ch<2;++ch)framePcm[ch]=(v.data[a*2+ch]+(v.data[b*2+ch]-v.data[a*2+ch])*fraction)*v.gain*v.envelope;v.physics->process(framePcm,1);for(unsigned ch=0;ch<2;++ch)target[i*2+ch]+=framePcm[ch];
    v.position+=v.step;
   }
  }
  if(legacyMode){dsp.setParameters({resonance.load(),tension.load(),strike.load(),friction.load()});dsp.process(target,count);}
  float l=0,r=0;for(unsigned k=0;k<count;++k){l=std::max(l,std::abs(target[k*2]));r=std::max(r,std::abs(target[k*2+1]));}peakL=l;peakR=r;
  active.store(live);for(unsigned i=0;i<count*2;++i)target[i]=std::clamp(target[i],-1.f,1.f);
 }
 void workerLoop(){CoInitializeEx(nullptr,COINIT_MULTITHREADED);dsp.reset(float(rate.load()));unsigned seenGeneration=0;std::array<float,256*2> block{};
  while(running.load()){
   {std::lock_guard lock(libraryMutex);if(seenGeneration!=generation){for(auto& v:voices)v.decoder.reset();seenGeneration=generation;round.fill(0);}}
   for(unsigned key=0;key<128;++key){if(deferred[key]&&!sustain)applyNote(key,0);unsigned n=notes[key].load(std::memory_order_acquire);if(n!=observed[key]){observed[key]=n;applyNote(key,n&127);}}
   unsigned w=write.load(std::memory_order_relaxed),r=read.load(std::memory_order_acquire);
   unsigned produce=wasapi_policy::productionFrames(w-r,ahead.load(std::memory_order_relaxed));if(produce){render(block.data(),produce);for(unsigned i=0;i<produce;++i){unsigned dest=(w+i)%RingFrames;ring[dest*2]=block[i*2];ring[dest*2+1]=block[i*2+1];}write.store(w+produce,std::memory_order_release);}else Sleep(1);
  }
  for(auto& v:voices)v.decoder.reset();CoUninitialize();
 }
 static void asioPull(void* context,void* left,void* right,uint32_t frames,AsioPcm leftType,AsioPcm rightType) noexcept {
  auto& s=*static_cast<Impl*>(context);unsigned r=s.read.load(std::memory_order_relaxed),w=s.write.load(std::memory_order_acquire),available=s.running.load(std::memory_order_acquire)?std::min<unsigned>(frames,w-r):0;if(available<frames)s.underruns.fetch_add(1,std::memory_order_relaxed);
  for(unsigned ch=0;ch<2;++ch){auto type=ch?rightType:leftType;auto* target=static_cast<unsigned char*>(ch?right:left);unsigned bytes=type==AsioPcm::Int16?2:type==AsioPcm::Int24?3:4;
   for(unsigned i=0;i<frames;++i){float value=i<available?s.ring[((r+i)%RingFrames)*2+ch]:0.f;auto* p=target+size_t(i)*bytes;if(type==AsioPcm::Float32)memcpy(p,&value,4);else{int32_t v=int32_t(std::clamp<double>(value,-1.,1.)*2147483647.);if(bytes==2){int16_t sample16=int16_t(v>>16);memcpy(p,&sample16,2);}else if(bytes==3){p[0]=BYTE(v>>8);p[1]=BYTE(v>>16);p[2]=BYTE(v>>24);}else memcpy(p,&v,4);}}
  }s.read.store(r+available,std::memory_order_release);
 }
 void outputLoop(std::promise<std::wstring> started){
  HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);IMMDeviceEnumerator* enumerator=nullptr;IMMDevice* device=nullptr;IAudioClient* client=nullptr;IAudioRenderClient* renderClient=nullptr;WAVEFORMATEX* format=nullptr;bool ready=false;UINT32 bufferFrames=0;
  auto clean=[&]{if(client)client->Stop();if(renderClient)renderClient->Release();if(format)CoTaskMemFree(format);if(client)client->Release();if(device)device->Release();if(enumerator)enumerator->Release();if(SUCCEEDED(co))CoUninitialize();};
  auto fail=[&](const wchar_t* what,HRESULT hr){auto s=hrText(what,hr);message(s);running.store(false);if(!ready)started.set_value(s);clean();};
  HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator));if(FAILED(hr)){fail(L"Cannot enumerate playback devices",hr);return;}
  hr=endpointId.empty()?enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device):enumerator->GetDevice(endpointId.c_str(),&device);if(FAILED(hr)){fail(L"Selected/default output device is unavailable",hr);return;}
  hr=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&client));if(FAILED(hr)){fail(L"Cannot activate WASAPI",hr);return;}
  hr=chooseFormat(client,mode,&format);if(FAILED(hr)){fail(L"No supported output format for the requested mode",hr);return;}
  bool floating=false;if(!pcmFormat(format,floating)){fail(L"Unsupported endpoint PCM encoding",E_INVALIDARG);return;}rate.store(format->nSamplesPerSec);
  REFERENCE_TIME defaultPeriod=0,minimumPeriod=0;hr=client->GetDevicePeriod(&defaultPeriod,&minimumPeriod);if(FAILED(hr)){fail(L"Cannot read endpoint periods",hr);return;}
  bool exclusive=mode==WasapiMode::Exclusive;REFERENCE_TIME period=exclusive?std::max<REFERENCE_TIME>(minimumPeriod,defaultPeriod):0;
  hr=client->Initialize(exclusive?AUDCLNT_SHAREMODE_EXCLUSIVE:AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,period,period,format,nullptr);
  if(exclusive&&hr==AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED){UINT32 aligned=0;hr=client->GetBufferSize(&aligned);if(FAILED(hr)||!aligned){fail(L"Cannot obtain required exclusive buffer alignment",FAILED(hr)?hr:E_UNEXPECTED);return;}period=wasapi_policy::alignedPeriod(aligned,format->nSamplesPerSec);client->Release();client=nullptr;hr=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&client));if(SUCCEEDED(hr))hr=client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,period,period,format,nullptr);}
  if(FAILED(hr)){fail(initializeReason(hr),hr);return;}
  hr=client->SetEventHandle(event);if(FAILED(hr)){fail(L"WASAPI event setup failed",hr);return;}
  hr=client->GetBufferSize(&bufferFrames);deviceFrames=bufferFrames;if(FAILED(hr)){fail(L"WASAPI buffer unavailable",hr);return;}if(!wasapi_policy::boundedBuffer(bufferFrames)){fail(L"Endpoint buffer exceeds supported 16384-frame limit",E_INVALIDARG);return;}ahead.store(std::max<unsigned>(Ahead,std::min<unsigned>(16384,bufferFrames*2)));
  hr=client->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void**>(&renderClient));if(FAILED(hr)){fail(L"WASAPI renderer unavailable",hr);return;}
  BYTE* initial=nullptr;hr=renderClient->GetBuffer(bufferFrames,&initial);if(SUCCEEDED(hr))hr=renderClient->ReleaseBuffer(bufferFrames,AUDCLNT_BUFFERFLAGS_SILENT);if(FAILED(hr)){fail(L"WASAPI priming failed",hr);return;}
  hr=client->Start();if(FAILED(hr)){fail(L"WASAPI playback start failed",hr);return;}
  ready=true;message(exclusive?L"WASAPI exclusive mode ready":L"WASAPI shared mode ready");started.set_value(L"");
  while(running.load()){
   DWORD wait=WaitForSingleObject(event,100);if(!running.load())break;if(wait==WAIT_TIMEOUT)continue;if(wait!=WAIT_OBJECT_0){audioError.store(HRESULT_FROM_WIN32(GetLastError()));break;}
   UINT32 padding=0;if(!exclusive){hr=client->GetCurrentPadding(&padding);if(FAILED(hr)||padding>bufferFrames){audioError.store(FAILED(hr)?hr:E_UNEXPECTED);break;}}
   UINT32 count=wasapi_policy::transferFrames(mode,bufferFrames,padding);if(!count)continue;BYTE* dest=nullptr;hr=renderClient->GetBuffer(count,&dest);if(FAILED(hr)){audioError.store(hr);break;}
   // Real-time transfer: fixed storage, atomics, arithmetic only. No file I/O, mutex, allocation or waiting.
   unsigned r=read.load(std::memory_order_relaxed),w=write.load(std::memory_order_acquire),available=std::min<unsigned>(count,w-r);
   if(available<count)++underruns;
   unsigned bytes=format->wBitsPerSample/8;
   for(unsigned i=0;i<count;++i)for(unsigned ch=0;ch<format->nChannels;++ch){float value=0;
    if(i<available){unsigned source=(r+i)%RingFrames;value=ch<2?ring[source*2+ch]:0.f;if(format->nChannels==1)value=(ring[source*2]+ring[source*2+1])*0.5f;}
    BYTE* p=dest+i*format->nBlockAlign+ch*bytes;
    if(floating)*reinterpret_cast<float*>(p)=value;else {int32_t val=static_cast<int32_t>(std::clamp<double>(value,-1.0,1.0)*2147483647.0);if(bytes==2){int16_t sample16=static_cast<int16_t>(val>>16);memcpy(p,&sample16,2);}else if(bytes==3){p[0]=BYTE(val>>8);p[1]=BYTE(val>>16);p[2]=BYTE(val>>24);}else memcpy(p,&val,4);}
   }
   read.store(r+available,std::memory_order_release);hr=renderClient->ReleaseBuffer(count,0);if(FAILED(hr)){audioError.store(hr);break;}
  }
  running.store(false);clean();
 }
};
bool AudioEngine::renderOffline(const std::vector<Sample>& samples,const std::wstring& provenance,bool confirmed,const std::vector<OfflineNote>& events,uint32_t rate,uint32_t frames,OfflineRender& result,std::wstring& error,const DspParameters& parameters){
 if(rate<8000||rate>384000||!frames||frames>4000000||events.size()>65536){error=L"Offline diagnostic exceeds rate, frame or event bounds.";return false;}
 uint32_t previous=0;for(const auto& event:events){if(event.frame<previous||event.frame>=frames||event.key<0||event.key>127||event.velocity<0||event.velocity>127){error=L"Offline events must be ordered and within frame/key/velocity bounds.";return false;}previous=event.frame;}
 AudioEngine engine;if(!engine.setSamples(samples,error,provenance,confirmed))return false;
 auto& state=*engine.impl;state.rate=rate;state.dsp.reset(float(rate));engine.setDspParameters(parameters);OfflineRender rendered;rendered.stereo.resize(size_t(frames)*2);rendered.selections.reserve(events.size());size_t next=0;
 for(uint32_t frame=0;frame<frames;){
  while(next<events.size()&&events[next].frame==frame){const auto& event=events[next++];int selected=state.applyNote(event.key,event.velocity);if(event.velocity)rendered.selections.push_back({frame,event.key,event.velocity,selected});unsigned live=0;for(auto& voice:state.voices)if(voice.decoder)++live;rendered.peakVoices=std::max(rendered.peakVoices,live);}
  uint32_t count=std::min<uint32_t>(256,frames-frame);if(next<events.size())count=std::min(count,events[next].frame-frame);
  state.render(rendered.stereo.data()+size_t(frame)*2,count);frame+=count;
 }
 rendered.voiceSteals=state.steals.load();rendered.decodeErrors=state.failures.load();
 if(rendered.decodeErrors){error=L"Offline rendering encountered decoder errors.";return false;}
 result=std::move(rendered);return true;
}
AudioEngine::AudioEngine():impl(std::make_unique<Impl>()){}
AudioEngine::~AudioEngine(){stop();}
bool AudioEngine::enumerateAsioDevices(std::vector<AsioDevice>& devices,std::wstring& error){return AsioBackend::enumerate(devices,error);}
bool AudioEngine::startAsio(const std::wstring& id,uint32_t requestedFrames,void* window,std::wstring& error){
 stop();impl->audioError=S_OK;for(auto& note:impl->notes)note=0;impl->observed.fill(0);impl->round.fill(0);for(auto& voice:impl->voices)voice.decoder.reset();impl->underruns=0;impl->failures=0;impl->steals=0;impl->write=0;impl->read=0;
 if(!impl->asio.prepare(id,requestedFrames,window,impl.get(),&Impl::asioPull,error)){impl->message(error);return false;}
 impl->ring=static_cast<float*>(VirtualAlloc(nullptr,Impl::RingBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!impl->ring){error=L"Cannot allocate ASIO resident PCM ring";stop();impl->message(error);return false;}std::fill(impl->ring,impl->ring+RingFrames*2,0.f);if(!VirtualLock(impl->ring,Impl::RingBytes)){error=hrText(L"ASIO playback refused: PCM residency failed",HRESULT_FROM_WIN32(GetLastError()));stop();impl->message(error);return false;}impl->ringLocked=true;
 impl->usingAsio=true;impl->deviceFrames=impl->asio.frames();impl->rate=impl->asio.sampleRate();impl->ahead=std::max<unsigned>(Ahead,std::min<unsigned>(RingFrames,impl->asio.frames()*2));impl->dsp.reset(float(impl->rate.load()));impl->running=true;impl->worker=std::thread(&Impl::workerLoop,impl.get());
 if(!impl->asio.start(error)){stop();impl->message(error);return false;}impl->message(L"ASIO output active (official Steinberg GPLv3 SDK host); first stereo output pair");return true;
}
bool AudioEngine::start(std::wstring& error){return start(L"",WasapiMode::Shared,error);}
bool AudioEngine::startLegacyWasapi(std::wstring& error){return start(error);}
void AudioEngine::setDspParameters(const DspParameters& parameters) noexcept {auto p=clampDspParameters(parameters);impl->resonance.store(p.resonance,std::memory_order_release);impl->tension.store(p.tension,std::memory_order_release);impl->strike.store(p.strike,std::memory_order_release);impl->friction.store(p.friction,std::memory_order_release);float values[]={p.hardness,p.strikeWindowMs,p.detuneCents,p.stiffness,p.decaySeconds,p.sympathetic,p.releaseNoise,p.blend};for(unsigned i=0;i<8;++i)impl->physics[i]=values[i];impl->physicsOn=p.physicsOn;impl->legacyMode=p.legacyMode;}
bool AudioEngine::start(const std::wstring& endpointId,WasapiMode mode,std::wstring& error){stop();impl->audioError=S_OK;if(mode!=WasapiMode::Shared&&mode!=WasapiMode::Exclusive){error=L"Invalid WASAPI mode";impl->message(error);return false;}impl->usingAsio=false;impl->endpointId=endpointId;impl->mode=mode;for(auto& note:impl->notes)note.store(0);impl->observed.fill(0);impl->round.fill(0);for(auto& voice:impl->voices)voice.decoder.reset();impl->dsp.reset(48000);impl->underruns=0;impl->failures=0;impl->steals=0;impl->ahead=Ahead;impl->ring=static_cast<float*>(VirtualAlloc(nullptr,Impl::RingBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!impl->ring){error=L"Cannot allocate fixed playback ring";impl->message(error);return false;}std::fill(impl->ring,impl->ring+RingFrames*2,0.f);if(!VirtualLock(impl->ring,Impl::RingBytes)){error=hrText(L"Playback refused: ring residency could not be established",HRESULT_FROM_WIN32(GetLastError()));stop();impl->message(error);return false;}impl->ringLocked=true;impl->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!impl->event){error=L"Cannot create audio event";stop();impl->message(error);return false;}impl->write=0;impl->read=0;impl->audioError=S_OK;impl->running=true;std::promise<std::wstring> promise;auto future=promise.get_future();impl->output=std::thread(&Impl::outputLoop,impl.get(),std::move(promise));error=future.get();if(!error.empty()){stop();return false;}impl->worker=std::thread(&Impl::workerLoop,impl.get());return true;}
void AudioEngine::stop(){bool wasRunning=impl->running.exchange(false);closeMidi();if(impl->event)SetEvent(impl->event);if(impl->worker.joinable())impl->worker.join();if(impl->output.joinable())impl->output.join();impl->asio.close();if(impl->event){CloseHandle(impl->event);impl->event=nullptr;}if(impl->ring){if(impl->ringLocked)VirtualUnlock(impl->ring,Impl::RingBytes);VirtualFree(impl->ring,0,MEM_RELEASE);impl->ring=nullptr;impl->ringLocked=false;}impl->active=0;if(wasRunning)impl->message(L"Audio stopped");}
bool AudioEngine::setSamples(const std::vector<Sample>& samples,std::wstring& error,const std::wstring& provenance,bool acousticSourceConfirmed){// Provenance remains metadata, never a playback gate.
 (void)provenance;(void)acousticSourceConfirmed;if(samples.size()>4096){error=L"Maximum 4096 samples per instrument";return false;}for(const auto& s:samples)if(!isPlaybackPrepared(s)){error=L"FLAC must finish PCM cache preparation before playback.";return false;}for(auto& s:samples)if(!s.rate||!s.frames||!validSampleParameters(s)||!std::isfinite(s.releaseSeconds)||s.releaseSeconds<0.001f||s.releaseSeconds>30.f||s.map.root<0||s.map.root>127||s.map.low<0||s.map.high>127||s.map.low>s.map.high||s.map.velLow<1||s.map.velHigh>127||s.map.velLow>s.map.velHigh){error=L"Invalid sample metadata or mapping";return false;}std::lock_guard lock(impl->libraryMutex);impl->samples=samples;++impl->generation;return true;}
void AudioEngine::noteOn(int key,int velocity){if(key<0||key>127)return;unsigned old=impl->notes[key].load(std::memory_order_relaxed);impl->notes[key].store(((old+256)&0xffffff00u)|std::clamp(velocity,0,127),std::memory_order_release);}
void AudioEngine::noteOff(int key){noteOn(key,0);}
void AudioEngine::panic(){for(int i=0;i<128;++i)noteOff(i);}
std::wstring AudioEngine::status(){if(impl->asio.prepared()&&impl->asio.serviceEvents()){stop();impl->message(L"ASIO stopped after driver reset/rate/overload notification. Reconnect explicitly.");}std::lock_guard lock(impl->messageMutex);std::wostringstream s;if(FAILED(impl->audioError.load()))s<<hrText(L"Audio output stopped (device changed/disconnected)",impl->audioError.load())<<L" | ";s<<impl->lastMessage<<L" | "<<impl->rate.load()<<L" Hz | voices "<<impl->active.load()<<L"/32 | underruns "<<impl->underruns.load()<<L" | decode errors "<<impl->failures.load()<<L" | voice steals "<<impl->steals.load();return s.str();}
bool AudioEngine::openMidi(unsigned device,std::wstring& error){closeMidi();MMRESULT result=midiInOpen(&impl->midi,device,reinterpret_cast<DWORD_PTR>(&Impl::midiCallback),reinterpret_cast<DWORD_PTR>(impl.get()),CALLBACK_FUNCTION);if(result!=MMSYSERR_NOERROR){wchar_t message[256]{};midiInGetErrorTextW(result,message,256);error=message;impl->midi=nullptr;return false;}result=midiInStart(impl->midi);if(result!=MMSYSERR_NOERROR){error=L"MIDI input start failed";closeMidi();return false;}return true;}
void AudioEngine::closeMidi(){if(impl->midi){midiInStop(impl->midi);midiInReset(impl->midi);midiInClose(impl->midi);impl->midi=nullptr;}panic();}
}











namespace whic {
bool AudioEngine::enumerateOutputDevices(std::vector<OutputDevice>& result,std::wstring& error){
 ProbeSession s;HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&s.enumeration));if(FAILED(hr)){error=hrText(L"Cannot enumerate output devices",hr);return false;}
 std::wstring defaultId;IMMDevice* preferred=nullptr;if(SUCCEEDED(s.enumeration->GetDefaultAudioEndpoint(eRender,eConsole,&preferred))){LPWSTR id=nullptr;if(SUCCEEDED(preferred->GetId(&id))){defaultId=id;CoTaskMemFree(id);}preferred->Release();}
 IMMDeviceCollection* collection=nullptr;hr=s.enumeration->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&collection);if(FAILED(hr)){error=hrText(L"Cannot read active playback endpoints",hr);return false;}UINT count=0;hr=collection->GetCount(&count);std::vector<OutputDevice> devices;
 if(SUCCEEDED(hr))for(UINT i=0;i<count;++i){IMMDevice* device=nullptr;if(FAILED(collection->Item(i,&device)))continue;LPWSTR id=nullptr;if(SUCCEEDED(device->GetId(&id))){OutputDevice item;item.id=id;item.name=id;item.isDefault=item.id==defaultId;CoTaskMemFree(id);IPropertyStore* properties=nullptr;if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&properties))){PROPVARIANT value;PropVariantInit(&value);if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&value))&&value.vt==VT_LPWSTR)item.name=value.pwszVal;PropVariantClear(&value);properties->Release();}devices.push_back(std::move(item));}device->Release();}
 collection->Release();if(FAILED(hr)){error=hrText(L"Cannot read playback endpoint count",hr);return false;}result=std::move(devices);error.clear();return true;
}
bool AudioEngine::inspectOutput(const std::wstring& id,WasapiMode mode,std::wstring& report,std::wstring& error){
 // Read-only capability inspection: deliberately no Initialize, Start, device changes or session creation.
 if(mode!=WasapiMode::Shared&&mode!=WasapiMode::Exclusive){error=L"Invalid WASAPI mode";return false;}ProbeSession s;HRESULT hr=s.open(id);if(FAILED(hr)){error=hrText(L"Cannot inspect selected playback endpoint",hr);return false;}WAVEFORMATEX* format=nullptr;hr=chooseFormat(s.client,mode,&format);if(FAILED(hr)){error=hrText(L"No supported format for requested mode",hr);return false;}REFERENCE_TIME normal=0,minimum=0;hr=s.client->GetDevicePeriod(&normal,&minimum);if(FAILED(hr)){CoTaskMemFree(format);error=hrText(L"Cannot read endpoint periods",hr);return false;}bool floating=false;pcmFormat(format,floating);std::wostringstream text;text<<(mode==WasapiMode::Shared?L"Shared":L"Exclusive")<<L" capability: "<<format->nSamplesPerSec<<L" Hz, "<<format->nChannels<<L" channels, "<<format->wBitsPerSample<<(floating?L"-bit float":L"-bit PCM")<<L"; default period "<<double(normal)/10000<<L" ms; minimum "<<double(minimum)/10000<<L" ms. Capability only: no audio stream initialized or started; exclusive availability/busy state remains untested.";CoTaskMemFree(format);report=text.str();error.clear();return true;
}
}












namespace whic {AudioTelemetry AudioEngine::telemetry()const noexcept{AudioTelemetry t;t.running=impl->running;t.asio=impl->usingAsio;t.rate=t.running?impl->rate.load():0;t.bufferFrames=t.running?impl->deviceFrames.load():0;t.voices=impl->active;t.underruns=impl->underruns;t.decodedBytes=impl->decodedBytes;t.left=t.running?impl->peakL.load():0;t.right=t.running?impl->peakR.load():0;for(unsigned i=0;i<128;++i)t.keys[i]=(impl->notes[i].load()&127)!=0;return t;}void AudioEngine::setSustain(bool enabled)noexcept{impl->sustain=enabled;}}
