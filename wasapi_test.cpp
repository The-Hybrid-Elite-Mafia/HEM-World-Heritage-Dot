// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#include "audio.h"
#include <iostream>
int wmain(){using namespace whic;unsigned failed=0;auto check=[&](bool pass,const wchar_t* label){std::wcout<<(pass?L"PASS ":L"FAIL ")<<label<<L"\n";if(!pass)++failed;};
 check(wasapi_policy::transferFrames(WasapiMode::Shared,512,100)==412,L"shared event fills only unpadded frames");check(wasapi_policy::transferFrames(WasapiMode::Exclusive,512,100)==512,L"exclusive event always uses full buffer, ignores padding");check(wasapi_policy::transferFrames(WasapiMode::Shared,512,513)==0,L"invalid shared padding does not underflow");check(wasapi_policy::alignedPeriod(480,48000)==100000,L"aligned exclusive duration equals period (480/48k=10ms)");check(wasapi_policy::alignedPeriod(441,44100)==100000,L"44.1k alignment policy");check(wasapi_policy::alignedPeriod(0,0)==0,L"invalid zero-rate alignment guarded");check(wasapi_policy::supportedPcm(48000,2,32,true)&&wasapi_policy::supportedPcm(44100,2,24,false)&&!wasapi_policy::supportedPcm(48000,2,24,true)&&!wasapi_policy::supportedPcm(48000,64,32,true),L"float32 and integer16/24/32 format bounds");check(wasapi_policy::boundedBuffer(16384)&&!wasapi_policy::boundedBuffer(16385)&&!wasapi_policy::boundedBuffer(0),L"callback buffer upper bound");
 check(wasapi_policy::productionFrames(16380,16384)==4&&wasapi_policy::productionFrames(16384,16384)==0&&wasapi_policy::productionFrames(0,16384)==256,L"producer fills exact ring capacity without overshoot");
 uint32_t wrap=0xffffff80u;check(uint32_t(wrap+256)%wasapi_policy::residentRingFrames==(wrap%wasapi_policy::residentRingFrames+256)%wasapi_policy::residentRingFrames,L"ring remains continuous across32-bit counter wrap");
 std::wstring invalidReport,invalidError;check(!AudioEngine::inspectOutput(L"WHIC-nonexistent-test-endpoint",WasapiMode::Shared,invalidReport,invalidError)&&!invalidError.empty(),L"invalid endpoint fails read-only inspection clearly");
 std::vector<OutputDevice> devices;std::wstring error,report;bool enumerated=AudioEngine::enumerateOutputDevices(devices,error);check(enumerated,L"read-only endpoint enumeration");if(!enumerated)std::wcout<<error<<L"\n";
 for(const auto& d:devices){std::wcout<<L"Endpoint: "<<d.name<<(d.isDefault?L" [default]":L"")<<L"\n";for(auto mode:{WasapiMode::Shared,WasapiMode::Exclusive}){if(AudioEngine::inspectOutput(d.id,mode,report,error))std::wcout<<report<<L"\n";else std::wcout<<L"Capability unavailable: "<<error<<L"\n";}}
 std::wcout<<L"No Initialize/Start calls, exclusive acquisition, endpoint changes, or audible tests were performed.\n";return failed?1:0;
}



