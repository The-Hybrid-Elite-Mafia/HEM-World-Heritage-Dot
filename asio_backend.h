// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace whic {
struct AsioDevice {std::wstring id,name;};
enum class AsioPcm {Float32,Int16,Int24,Int32};
using AsioPull = void(*)(void*,void*,void*,uint32_t,AsioPcm,AsioPcm) noexcept;
// Uses official Steinberg GPLv3 IASIO declarations. Control-thread lifecycle only.
class AsioBackend {
public:
 // Definition is linked only into the silent test executable.
 static bool callbackSelfTest(std::wstring& error);
 AsioBackend();~AsioBackend();AsioBackend(const AsioBackend&)=delete;AsioBackend& operator=(const AsioBackend&)=delete;
 static bool enumerate(std::vector<AsioDevice>&,std::wstring& error); // Registry only; never loads a driver.
 bool prepare(const std::wstring& registeredId,uint32_t requestedFrames,void* window,void* context,AsioPull,std::wstring& error);
 bool start(std::wstring& error);void close() noexcept;
 uint32_t sampleRate() const noexcept;uint32_t frames() const noexcept;uint32_t pendingEvents() const noexcept;uint32_t serviceEvents() noexcept; // Control-thread latency refresh; fatal events stay latched.
 bool prepared() const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl;
};
}


