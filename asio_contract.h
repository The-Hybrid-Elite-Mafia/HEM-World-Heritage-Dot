// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#pragma once
// Original WHIC internal host contracts. NOT an ASIO ABI, SDK substitute, driver,
// or working ASIO backend. No Steinberg declarations/code are reproduced here.
// A separately authorized SDK adapter must translate its native API into these
// contracts. All setup/teardown is control-thread-only; render is noexcept,
// bounded, allocation/file-I/O/lock-free and operates on prepared PCM only.
#include <atomic>
#include <cstdint>
#include <cmath>
#include <limits>

namespace whic::device_contract {
enum class Gate : std::uint8_t { license_required, sdk_unavailable, no_driver, ready };
struct Availability {
 bool license_authorized=false, sdk_built=false, driver_present=false;
 constexpr Gate gate() const noexcept {
  if(!license_authorized)return Gate::license_required;
  if(!sdk_built)return Gate::sdk_unavailable;
  if(!driver_present)return Gate::no_driver;
  return Gate::ready;
 }
};
enum class SampleType : std::uint8_t { float32_le, int16_le, int24_le, int32_le, unsupported };
struct BufferLimits {std::uint32_t minimum=0,maximum=0,preferred=0;std::int32_t granularity=0;};
// granularity -1: powers of two; 0: fixed preferred; positive: min+n*step.
constexpr bool accepts(BufferLimits b,std::uint32_t frames) noexcept {
 if(!b.minimum||b.minimum>b.maximum||b.maximum>65536||frames<b.minimum||frames>b.maximum)return false;
 if(b.granularity==-1)return frames && !(frames&(frames-1));
 if(b.granularity==0)return frames==b.preferred;
 return b.granularity>0 && (frames-b.minimum)%std::uint32_t(b.granularity)==0;
}
struct Configuration {double sample_rate=0;std::uint32_t frames=0,left_channel=0,right_channel=1,available_outputs=0;SampleType type=SampleType::unsupported;};
inline bool valid(Configuration c,BufferLimits limits) noexcept {
 return std::isfinite(c.sample_rate)&&c.sample_rate>=8000&&c.sample_rate<=384000&&accepts(limits,c.frames)&&c.available_outputs>=2&&c.left_channel<c.available_outputs&&c.right_channel<c.available_outputs&&c.left_channel!=c.right_channel&&c.type!=SampleType::unsupported;
}
struct Latency {std::uint32_t driver_output_frames=0,application_queue_frames=0;double sample_rate=0;
 double estimated_output_ms()const noexcept {return std::isfinite(sample_rate)&&sample_rate>0?1000.0*(double(driver_output_frames)+application_queue_frames)/sample_rate:std::numeric_limits<double>::quiet_NaN();}
}; // Estimate only: excludes MIDI input, converters, hardware and measured roundtrip.
enum class Event : std::uint32_t { none=0, reset=1, rate_changed=2, latency_changed=4, overload=8 };
struct ControlMailbox {
 static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
 std::atomic<std::uint32_t> pending{0};
 void signal(Event e) noexcept {pending.fetch_or(std::uint32_t(e),std::memory_order_release);}
 std::uint32_t take()noexcept{return pending.exchange(0,std::memory_order_acq_rel);}
};
enum class State : std::uint8_t { closed, configured, running, stopped, faulted };
// Control-thread state machine only. Adapter must stop callbacks and ensure all
// in-flight render calls finish before disposing buffers or changing PCM data.
class Lifecycle {
 State value_=State::closed;
public:
 State state()const noexcept{return value_;}
 bool configure(Availability a,Configuration c,BufferLimits b)noexcept{if((value_!=State::closed&&value_!=State::stopped)||a.gate()!=Gate::ready||!valid(c,b))return false;value_=State::configured;return true;}
 bool start()noexcept{if(value_!=State::configured&&value_!=State::stopped)return false;value_=State::running;return true;}
 bool stop()noexcept{if(value_!=State::running)return false;value_=State::stopped;return true;}
 void fault()noexcept{value_=State::faulted;}
 bool close(bool callbacks_quiescent)noexcept{if(!callbacks_quiescent||value_==State::running)return false;value_=State::closed;return true;}
};
// Render consumes fixed prepared state into adapter-owned planar stereo float.
// The SDK adapter handles format conversion with preallocated channel buffers.
using Render = void(*)(void* context,float* left,float* right,std::uint32_t frames) noexcept;
struct Renderer {void* context=nullptr;Render render=nullptr;};
}
