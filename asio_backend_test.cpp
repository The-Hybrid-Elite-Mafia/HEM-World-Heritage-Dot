// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#include "audio.h"
#include <iostream>
int wmain(){
#ifdef WHIC_ASIO_BACKEND_TESTING
std::wstring callbackError;if(!whic::AsioBackend::callbackSelfTest(callbackError)){std::wcerr<<callbackError;return 4;}std::wcout<<L"PASS official SDK callback routing, silence, notifications, recursion, concurrent entry and lifetime tests (no driver).\n";
#endif
std::vector<whic::AsioDevice> devices;std::wstring error;if(!whic::AudioEngine::enumerateAsioDevices(devices,error)){std::wcerr<<error<<L"\n";return 1;}std::wcout<<L"Official Steinberg GPLv3 SDK host compiled. Registered 64-bit ASIO devices: "<<devices.size()<<L"\n";for(auto& d:devices)std::wcout<<d.name<<L"\n";whic::AudioEngine engine;
 const std::wstring missing=L"{00000000-0000-0000-0000-000000000000}";for(auto& d:devices)if(d.id==missing){std::wcerr<<L"Reserved missing-driver test ID unexpectedly registered; skipping negative test\n";return 2;}
 if(engine.startAsio(missing,0,nullptr,error)||error.find(L"not installed")==std::wstring::npos){std::wcerr<<L"Missing-driver guard failed: "<<error;return 3;}std::wcout<<L"PASS missing-driver guard: "<<error<<L"\n"<<L"Registry-only discovery and unregistered-ID rejection; no driver loaded, initialized or started. No audio hardware or audible test.\n";return 0;}


