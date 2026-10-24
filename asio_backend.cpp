// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "asio_backend.h"
#include <windows.h>
#include <objbase.h>
// Official ASIO structs use4-byte packing; SDK emits pragma itself on MSVC.
#ifndef _MSC_VER
#pragma pack(push,4)
#endif
#include "third_party/asio/include/iasiodrv.h"
#ifndef _MSC_VER
#pragma pack(pop)
#endif
#include <atomic>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#ifdef WHIC_ASIO_BACKEND_TESTING
#include <thread>
#endif
static_assert(sizeof(void*)==8&&sizeof(long)==4,"This ASIO host targets Windows x64");
static_assert(sizeof(ASIOBufferInfo)==24&&sizeof(ASIOCallbacks)==32&&alignof(ASIOTime)==4&&sizeof(ASIOTime)==148,"Official SDK4-byte packing required");
namespace whic {
namespace {
std::wstring errorCode(const wchar_t* why,long code){return std::wstring(why)+L" (code "+std::to_wstring(code)+L")";}
bool readString(HKEY key,const wchar_t* value,std::wstring& out){std::array<wchar_t,4096> text{};DWORD bytes=DWORD(text.size()*sizeof(wchar_t)),type=0;auto result=RegQueryValueExW(key,value,nullptr,&type,reinterpret_cast<BYTE*>(text.data()),&bytes);if(result!=ERROR_SUCCESS||type!=REG_SZ||bytes<2||bytes>text.size()*2)return false;text.back()=0;out=text.data();return !out.empty();}
bool outputType(ASIOSampleType sdkType,AsioPcm& type,unsigned& bytes){switch(sdkType){case ASIOSTFloat32LSB:type=AsioPcm::Float32;bytes=4;return true;case ASIOSTInt16LSB:type=AsioPcm::Int16;bytes=2;return true;case ASIOSTInt24LSB:type=AsioPcm::Int24;bytes=3;return true;case ASIOSTInt32LSB:type=AsioPcm::Int32;bytes=4;return true;default:return false;}}
bool validFrames(long value,long low,long high,long preferred,long granularity){if(value<1||value>16384||low<1||high<low||value<low||value>high)return false;if(granularity==-1){long multiple=value/low;return value%low==0&&multiple>0&&!(multiple&(multiple-1));}if(granularity==0)return value==preferred;return granularity>0&&(value-low)%granularity==0;}
}
struct AsioBackend::Impl {
 IASIO* driver=nullptr;std::array<ASIOBufferInfo,2> buffers{};std::array<AsioPcm,2> formats{};std::array<unsigned,2> bytes{};ASIOCallbacks callbacks{};
 uint32_t rate=0,block=0;void* context=nullptr;AsioPull pull=nullptr;bool created=false,started=false,com=false,outputReady=false;
 std::atomic_flag renderBusy=ATOMIC_FLAG_INIT;std::atomic<bool> ready{false},accepting{false};std::atomic<uint32_t> events{0};
 static inline std::atomic<Impl*> current{nullptr};static inline std::atomic<uint32_t> inFlight{0};
 static_assert(std::atomic<Impl*>::is_always_lock_free&&std::atomic<uint32_t>::is_always_lock_free);
 struct CallbackGuard {Impl* target;CallbackGuard(){inFlight.fetch_add(1);target=current.load();}~CallbackGuard(){inFlight.fetch_sub(1);}};
 static void switchBuffer(long index,ASIOBool){CallbackGuard guard;auto* p=guard.target;if(!p||index<0||index>1||!p->ready.load(std::memory_order_acquire))return;if(p->renderBusy.test_and_set(std::memory_order_acquire)){p->events.fetch_or(4,std::memory_order_release);return;}struct ReleaseRender{std::atomic_flag& flag;~ReleaseRender(){flag.clear(std::memory_order_release);}} release{p->renderBusy};
  if(p->accepting.load(std::memory_order_acquire)&&(p->events.load(std::memory_order_relaxed)&7)==0)p->pull(p->context,p->buffers[0].buffers[index],p->buffers[1].buffers[index],p->block,p->formats[0],p->formats[1]);
  else for(unsigned c=0;c<2;++c)memset(p->buffers[c].buffers[index],0,size_t(p->block)*p->bytes[c]);
  if(p->outputReady)p->driver->outputReady();
 }
 static ASIOTime* switchTime(ASIOTime*,long index,ASIOBool direct){switchBuffer(index,direct);return nullptr;}
 static void changedRate(ASIOSampleRate){CallbackGuard guard;if(guard.target)guard.target->events.fetch_or(2,std::memory_order_release);}
 static long message(long selector,long value,void*,double*){if(selector==kAsioSelectorSupported)return value==kAsioResetRequest||value==kAsioResyncRequest||value==kAsioLatenciesChanged||value==kAsioEngineVersion||value==kAsioSupportsTimeInfo||value==kAsioOverload;
  if(selector==kAsioEngineVersion)return 2;if(selector==kAsioSupportsTimeInfo)return 1;CallbackGuard guard;auto* p=guard.target;if(!p)return 0;
  if(selector==kAsioLatenciesChanged){p->events.fetch_or(8,std::memory_order_release);return 1;}if(selector==kAsioResetRequest||selector==kAsioResyncRequest||selector==kAsioOverload){p->events.fetch_or(selector==kAsioOverload?4:1,std::memory_order_release);return 1;}return 0;
 }
 void close() noexcept {accepting=false;if(driver)driver->stop();Impl* expected=this;bool owned=current.compare_exchange_strong(expected,nullptr);if(owned)while(inFlight.load()!=0)Sleep(1);ready=false;if(driver&&created)driver->disposeBuffers();created=false;started=false;if(driver){driver->Release();driver=nullptr;}if(com){CoUninitialize();com=false;}rate=block=0;}
};
AsioBackend::AsioBackend():impl(std::make_unique<Impl>()){}AsioBackend::~AsioBackend(){close();}
bool AsioBackend::enumerate(std::vector<AsioDevice>& result,std::wstring& error){std::vector<AsioDevice> found;HKEY root=nullptr;LONG rc=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\ASIO",0,KEY_READ|KEY_WOW64_64KEY,&root);if(rc==ERROR_FILE_NOT_FOUND){result.clear();error.clear();return true;}if(rc!=ERROR_SUCCESS){error=errorCode(L"Cannot read installed 64-bit ASIO registrations",rc);return false;}
 for(DWORD i=0;i<1024;++i){std::array<wchar_t,256> name{};DWORD chars=DWORD(name.size());rc=RegEnumKeyExW(root,i,name.data(),&chars,nullptr,nullptr,nullptr,nullptr);if(rc==ERROR_NO_MORE_ITEMS)break;if(rc!=ERROR_SUCCESS)continue;HKEY key=nullptr;if(RegOpenKeyExW(root,name.data(),0,KEY_READ,&key)!=ERROR_SUCCESS)continue;AsioDevice device;device.name=name.data();readString(key,L"Description",device.name);if(readString(key,L"CLSID",device.id)){CLSID parsed{};if(SUCCEEDED(CLSIDFromString(device.id.c_str(),&parsed)))found.push_back(std::move(device));}RegCloseKey(key);}
 RegCloseKey(root);result=std::move(found);error.clear();return true;
}
bool AsioBackend::prepare(const std::wstring& id,uint32_t requested,void* window,void* context,AsioPull pull,std::wstring& error){close();if(!pull){error=L"ASIO render callback is missing";return false;}std::vector<AsioDevice> installed;if(!enumerate(installed,error))return false;if(std::none_of(installed.begin(),installed.end(),[&](auto& device){return _wcsicmp(device.id.c_str(),id.c_str())==0;})){error=L"Selected 64-bit ASIO driver is not installed. WASAPI remains available; no driver was installed.";return false;}
 auto& s=*impl;Impl* empty=nullptr;if(!Impl::current.compare_exchange_strong(empty,&s)){error=L"Only one ASIO driver may be active in this application";return false;}
 auto fail=[&](const wchar_t* why,long code){error=errorCode(why,code);close();return false;};HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);s.com=SUCCEEDED(hr);if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)return fail(L"ASIO COM initialization failed",hr);CLSID clsid{};hr=CLSIDFromString(id.c_str(),&clsid);if(FAILED(hr))return fail(L"Invalid registered ASIO CLSID",hr);
 hr=CoCreateInstance(clsid,nullptr,CLSCTX_INPROC_SERVER,clsid,reinterpret_cast<void**>(&s.driver));if(FAILED(hr))return fail(L"Installed ASIO driver could not load",hr);if(!s.driver->init(window))return fail(L"ASIO driver initialization failed",ASE_HWMalfunction);
 long inputs=0,outputs=0;auto code=s.driver->getChannels(&inputs,&outputs);if(code!=ASE_OK||outputs<2)return fail(L"ASIO requires at least two output channels",code);ASIOSampleRate rate=0;code=s.driver->getSampleRate(&rate);if(code!=ASE_OK||!std::isfinite(rate)||rate<8000||rate>384000||std::abs(rate-std::round(rate))>0.0001)return fail(L"Unsupported ASIO current sample rate; configure the driver first",code);if(std::llround(rate)!=48000)return fail(L"This workstation requests 48 kHz; driver currently differs. No rate or global setting was changed",ASE_NoClock);s.rate=uint32_t(std::llround(rate));
 long low=0,high=0,preferred=0,granularity=0;code=s.driver->getBufferSize(&low,&high,&preferred,&granularity);long frames=requested?long(requested):preferred;if(code!=ASE_OK||!validFrames(frames,low,high,preferred,granularity))return fail(L"Unsupported ASIO buffer size (maximum 16384 frames)",code);s.block=uint32_t(frames);
 for(long c=0;c<2;++c){ASIOChannelInfo info{};info.channel=c;info.isInput=ASIOFalse;code=s.driver->getChannelInfo(&info);if(code!=ASE_OK||!outputType(info.type,s.formats[c],s.bytes[c]))return fail(L"ASIO output format unsupported; use little-endian float32 or full-width PCM16/24/32",code);s.buffers[c].isInput=ASIOFalse;s.buffers[c].channelNum=c;}
 s.context=context;s.pull=pull;s.events=0;s.callbacks={&Impl::switchBuffer,&Impl::changedRate,&Impl::message,&Impl::switchTime};s.created=true;code=s.driver->createBuffers(s.buffers.data(),2,frames,&s.callbacks);if(code!=ASE_OK)return fail(L"ASIO output buffer creation failed",code);
 for(unsigned c=0;c<2;++c)for(unsigned half=0;half<2;++half){if(!s.buffers[c].buffers[half])return fail(L"ASIO returned null output buffers",ASE_HWMalfunction);memset(s.buffers[c].buffers[half],0,size_t(frames)*s.bytes[c]);}
 s.outputReady=s.driver->outputReady()==ASE_OK;s.ready.store(true,std::memory_order_release);error.clear();return true;
}
bool AsioBackend::start(std::wstring& error){auto& s=*impl;if(!s.created||!s.driver||s.started){error=L"ASIO must be prepared exactly once before start";return false;}s.accepting=true;auto code=s.driver->start();if(code!=ASE_OK){s.accepting=false;error=errorCode(L"ASIO start failed",code);close();return false;}s.started=true;error.clear();return true;}
void AsioBackend::close() noexcept {impl->close();}uint32_t AsioBackend::sampleRate()const noexcept{return impl->rate;}uint32_t AsioBackend::frames()const noexcept{return impl->block;}uint32_t AsioBackend::pendingEvents()const noexcept{return impl->events.load(std::memory_order_acquire);}uint32_t AsioBackend::serviceEvents() noexcept {auto events=impl->events.load(std::memory_order_acquire);if((events&8)&&impl->driver){long input=0,output=0;impl->driver->getLatencies(&input,&output);impl->events.fetch_and(~uint32_t(8),std::memory_order_acq_rel);}return impl->events.load(std::memory_order_acquire)&7;}bool AsioBackend::prepared()const noexcept{return impl->created;}
}




#ifdef WHIC_ASIO_BACKEND_TESTING
namespace whic {
bool AsioBackend::callbackSelfTest(std::wstring& error){
 if(!validFrames(192,48,768,192,-1)||validFrames(256,48,768,192,-1)||!validFrames(256,64,1024,256,-1)||validFrames(0,64,1024,256,-1)){error=L"SDK buffer granularity boundary policy failed";return false;}
 Impl test;Impl* empty=nullptr;if(!Impl::current.compare_exchange_strong(empty,&test)){error=L"Cannot run silent callback tests while a driver is active";return false;}
 struct Reset {Impl& instance;~Reset(){instance.accepting=false;Impl::current=nullptr;while(Impl::inFlight.load())Sleep(1);}} reset{test};
 std::array<float,16> left{},right{};test.block=8;test.formats={AsioPcm::Float32,AsioPcm::Float32};test.bytes={4,4};test.buffers[0].buffers[0]=left.data();test.buffers[0].buffers[1]=left.data()+8;test.buffers[1].buffers[0]=right.data();test.buffers[1].buffers[1]=right.data()+8;
 struct Context {unsigned calls=0;bool recurse=false,hold=false;std::atomic<bool> entered{false},release{false};} context;
 test.context=&context;test.pull=[](void* raw,void* l,void* r,uint32_t frames,AsioPcm,AsioPcm)noexcept{auto& c=*static_cast<Context*>(raw);++c.calls;for(unsigned i=0;i<frames;++i){static_cast<float*>(l)[i]=.25f;static_cast<float*>(r)[i]=-.5f;}if(c.recurse)Impl::switchBuffer(0,ASIOTrue);if(c.hold){c.entered=true;while(!c.release.load())Sleep(1);}};
 auto require=[&](bool value,const wchar_t* why){if(!value)error=why;return value;};test.ready=true;test.accepting=true;
 Impl::switchBuffer(0,ASIOTrue);if(!require(context.calls==1&&left[0]==.25f&&right[7]==-.5f&&left[8]==0,L"SDK callback stereo/half0 routing failed"))return false;
 Impl::switchTime(nullptr,1,ASIOTrue);if(!require(context.calls==2&&left[8]==.25f&&right[15]==-.5f,L"SDK time-info/half1 routing failed"))return false;
 Impl::switchBuffer(-1,ASIOTrue);Impl::switchBuffer(2,ASIOTrue);if(!require(context.calls==2,L"Invalid callback index was not rejected"))return false;
 test.accepting=false;Impl::switchBuffer(0,ASIOTrue);if(!require(left[0]==0&&right[7]==0&&context.calls==2,L"Stopped callback did not zero output"))return false;
 test.accepting=true;Impl::changedRate(44100);Impl::switchBuffer(1,ASIOTrue);if(!require(test.events.load()==2&&left[8]==0&&context.calls==2,L"Rate-change callback did not latch/silence"))return false;
 test.events=0;context.recurse=true;Impl::switchBuffer(0,ASIOTrue);if(!require(context.calls==3&&(test.events.load()&4)&&Impl::inFlight.load()==0,L"Recursive SDK callback was not rejected safely"))return false;
 context.recurse=false;context.hold=true;test.events=0;std::thread first([]{Impl::switchBuffer(0,ASIOTrue);});ULONGLONG until=GetTickCount64()+1000;while(!context.entered.load()&&GetTickCount64()<until)Sleep(1);bool entered=context.entered.load();if(entered)Impl::switchBuffer(1,ASIOTrue);context.release=true;first.join();if(!require(entered&&context.calls==4&&(test.events.load()&4),L"Concurrent SDK callback was not rejected safely"))return false;
 test.events=0;if(!require(Impl::message(kAsioEngineVersion,0,nullptr,nullptr)==2&&Impl::message(kAsioSupportsTimeInfo,0,nullptr,nullptr)==1&&Impl::message(kAsioResetRequest,0,nullptr,nullptr)==1&&test.events.load()==1,L"SDK message dispatch failed"))return false;
 test.events=0;context.hold=false;Impl::message(kAsioLatenciesChanged,0,nullptr,nullptr);Impl::switchBuffer(1,ASIOTrue);if(!require(test.events.load()==8&&context.calls==5,L"Latency-only notification incorrectly silenced the callback"))return false;
 Impl::current=nullptr;Impl::switchBuffer(0,ASIOTrue);Impl::changedRate(0);if(!require(context.calls==5&&Impl::inFlight.load()==0,L"Late callback lifetime guard failed"))return false;error.clear();return true;
}
}
#endif





