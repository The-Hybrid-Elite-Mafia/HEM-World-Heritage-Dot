// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#pragma once
#include "sample.h"
#include "dsp.h"
#include "asio_backend.h"
#include <memory>
#include <string>
#include <vector>
namespace whic {
enum class WasapiMode { Shared, Exclusive };
struct OutputDevice { std::wstring id,name; bool isDefault=false; };
namespace wasapi_policy {
inline constexpr uint32_t residentRingFrames=16384;
constexpr uint32_t productionFrames(uint32_t occupied,uint32_t target) noexcept {return occupied>=target?0:(target-occupied<256?target-occupied:256);}
constexpr uint32_t transferFrames(WasapiMode mode,uint32_t buffer,uint32_t padding) noexcept {return mode==WasapiMode::Exclusive?buffer:(padding<=buffer?buffer-padding:0);}
constexpr int64_t alignedPeriod(uint32_t frames,uint32_t rate) noexcept {return rate?(int64_t(frames)*10000000+rate/2)/rate:0;}
constexpr bool supportedPcm(uint32_t rate,unsigned channels,unsigned bits,bool floating) noexcept {return rate>=8000&&rate<=384000&&channels>=1&&channels<=32&&(floating?bits==32:(bits==16||bits==24||bits==32));}
constexpr bool boundedBuffer(uint32_t frames) noexcept {return frames>0&&frames<=16384;}
}
struct OfflineNote { uint32_t frame=0; int key=60,velocity=0; };
struct OfflineSelection { uint32_t frame=0; int key=0,velocity=0,sampleIndex=-1; };
struct OfflineRender { std::vector<float> stereo; std::vector<OfflineSelection> selections; unsigned peakVoices=0; uint64_t voiceSteals=0,decodeErrors=0,limitedFrames=0; float outputLeft=0,outputRight=0; };
struct AudioTelemetry {bool running=false,asio=false;unsigned rate=0,bufferFrames=0,voices=0;uint64_t underruns=0,decodedBytes=0,limitedFrames=0;float left=0,right=0;std::array<bool,128> keys{};};
class AudioEngine {
public:
 AudioEngine(); ~AudioEngine();
 // Diagnostic/offline path: same selector and renderer; never creates an audio device.
 static bool renderOffline(const std::vector<Sample>&,const std::wstring& provenance,bool acousticSourceConfirmed,const std::vector<OfflineNote>&,uint32_t rate,uint32_t frames,OfflineRender&,std::wstring& error,const DspParameters& dsp = {},float outputDb=0);
 static bool enumerateAsioDevices(std::vector<AsioDevice>& devices,std::wstring& error);
 bool startAsio(const std::wstring& driverId,uint32_t requestedFrames,void* nativeWindow,std::wstring& error);
 bool start(std::wstring& error); // Default endpoint, WASAPI shared mode.
 bool start(const std::wstring& endpointId,WasapiMode,std::wstring& error);
 bool startLegacyWasapi(std::wstring& error); // Compatibility alias for default shared.
 static bool enumerateOutputDevices(std::vector<OutputDevice>&,std::wstring& error);
 static bool inspectOutput(const std::wstring& endpointId,WasapiMode,std::wstring& report,std::wstring& error);
 AudioTelemetry telemetry() const noexcept; void setSustain(bool) noexcept;
 void setOutputDb(float) noexcept;
 void stop(); void setDspParameters(const DspParameters&) noexcept;
 bool setSamples(const std::vector<Sample>& samples,std::wstring& error,const std::wstring& provenance,bool acousticSourceConfirmed);
 void noteOn(int key,int velocity); void noteOff(int key); void panic();
 std::wstring status(); bool openMidi(unsigned device,std::wstring& error); void closeMidi();
private: struct Impl; std::unique_ptr<Impl> impl;
};
}








