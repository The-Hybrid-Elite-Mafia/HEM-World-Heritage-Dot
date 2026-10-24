// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>

namespace whic {
// Exactly one producer worker and one consumer callback. No callback allocation,
// locks, waits, mapping or filesystem work. initialize/close/destruction require
// both threads stopped and joined; callbacks must never outlive this object.
class ResidentDoubleBuffer {
public:
 static constexpr std::uint32_t maxFrames=2048,channels=2,slotCount=2;
 static constexpr std::size_t allocationBytes=std::size_t(maxFrames)*channels*slotCount*sizeof(float);
 enum : std::uint32_t { empty=0,writing=1,ready=2,reading=3 };
 struct WriteClaim {float* samples=nullptr;std::uint32_t slot=0;explicit operator bool()const noexcept{return samples!=nullptr;}};
 // Worker-scoped ownership. No allocation; exceptions/early returns cancel an
 // unpublished slot. Owner must outlive every lease; close only after producer
 // is joined and leases destroyed. Nonmovable return uses guaranteed elision.
 class ProducerLease {
  friend class ResidentDoubleBuffer;
  ResidentDoubleBuffer* owner_=nullptr;WriteClaim claim_{};
  explicit ProducerLease(ResidentDoubleBuffer& owner)noexcept:owner_(&owner),claim_(owner.producerClaim()){}
 public:
  ProducerLease(const ProducerLease&)=delete;
  ProducerLease& operator=(const ProducerLease&)=delete;
  ProducerLease(ProducerLease&&)=delete;
  ProducerLease& operator=(ProducerLease&&)=delete;
  ~ProducerLease(){if(owner_&&claim_)owner_->producerCancel(claim_);}
  explicit operator bool()const noexcept{return bool(claim_);}
  float* samples()const noexcept{return claim_.samples;}
  bool publish(std::uint32_t frames)noexcept{
   if(!owner_||!claim_||!owner_->producerPublish(claim_,frames))return false;
   claim_={};owner_=nullptr;return true;
  }
 };
private:
 struct alignas(64) Slot {std::atomic<std::uint32_t> state{empty};std::uint32_t frames=0;};
 static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
 Slot slots_[slotCount];float* storage_=nullptr;bool metadataLocked_=false;
 std::uint32_t producer_=0,consumer_=0,readOffset_=0;
 std::atomic<std::uint32_t> underruns_{0};
public:
 ResidentDoubleBuffer()=default;
 ResidentDoubleBuffer(const ResidentDoubleBuffer&)=delete;
 ResidentDoubleBuffer& operator=(const ResidentDoubleBuffer&)=delete;
 ~ResidentDoubleBuffer(){close();} // must run only after worker/callback quiescence
 ProducerLease producerLease()noexcept{return ProducerLease(*this);}
 bool initialize(std::wstring&error){
  if(storage_){error=L"Resident buffers already initialized.";return false;}
  void*p=VirtualAlloc(nullptr,allocationBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
  if(!p){error=L"VirtualAlloc failed: "+std::to_wstring(GetLastError());return false;}
  std::memset(p,0,allocationBytes); // prefault every page on control thread
  if(!VirtualLock(p,allocationBytes)){DWORD e=GetLastError();VirtualFree(p,0,MEM_RELEASE);error=L"VirtualLock failed: "+std::to_wstring(e)+L". Resident audio buffers unavailable.";return false;}
  // Lock callback metadata too, including the data pointer, atomic states and
  // consumer offsets. This object must not move until after close(). Driver
  // destination pages and executable code/stack residency belong to the adapter.
  if(!VirtualLock(this,sizeof(*this))){DWORD e=GetLastError();VirtualUnlock(p,allocationBytes);VirtualFree(p,0,MEM_RELEASE);error=L"VirtualLock metadata failed: "+std::to_wstring(e);return false;}
  metadataLocked_=true;
  storage_=static_cast<float*>(p);producer_=consumer_=readOffset_=0;underruns_.store(0);for(auto&s:slots_){s.frames=0;s.state.store(empty);}error.clear();return true;
 }
 void close()noexcept{if(storage_){VirtualUnlock(storage_,allocationBytes);VirtualFree(storage_,0,MEM_RELEASE);storage_=nullptr;}if(metadataLocked_){metadataLocked_=false;VirtualUnlock(this,sizeof(*this));}}
 WriteClaim producerClaim()noexcept{
  if(!storage_)return {};auto&s=slots_[producer_];std::uint32_t expected=empty;
  if(!s.state.compare_exchange_strong(expected,writing,std::memory_order_acquire,std::memory_order_relaxed))return {};
  return {storage_+std::size_t(producer_)*maxFrames*channels,producer_};
 }
 bool producerPublish(WriteClaim claim,std::uint32_t frames)noexcept{
  if(!storage_||claim.slot!=producer_||claim.samples!=storage_+std::size_t(producer_)*maxFrames*channels||!frames||frames>maxFrames)return false;
  auto&s=slots_[producer_];if(s.state.load(std::memory_order_relaxed)!=writing)return false;
  s.frames=frames;s.state.store(ready,std::memory_order_release);producer_=(producer_+1)%slotCount;return true;
 }
 bool producerCancel(WriteClaim claim)noexcept{
  if(!storage_||claim.slot!=producer_||claim.samples!=storage_+std::size_t(producer_)*maxFrames*channels)return false;
  auto&s=slots_[producer_];if(s.state.load(std::memory_order_relaxed)!=writing)return false;s.state.store(empty,std::memory_order_release);return true;
 }
 // Returns supplied frames, zero-fills any underrun. Requested size must be <=
 // maxFrames; out must point to requested*2 writable floats. At most two slots.
 std::uint32_t consume(float*out,std::uint32_t requested)noexcept{
  if(!out||requested>maxFrames)return 0;
  std::fill_n(out,std::size_t(requested)*channels,0.f);if(!storage_)return 0;
  std::uint32_t done=0;
  for(unsigned visited=0;visited<slotCount&&done<requested;++visited){
   auto&s=slots_[consumer_];auto state=s.state.load(std::memory_order_acquire);
   if(state==ready){s.state.store(reading,std::memory_order_relaxed);readOffset_=0;}
   else if(state!=reading)break;
   auto count=std::min(requested-done,s.frames-readOffset_);
   std::memcpy(out+std::size_t(done)*channels,storage_+(std::size_t(consumer_)*maxFrames+readOffset_)*channels,std::size_t(count)*channels*sizeof(float));
   done+=count;readOffset_+=count;
   if(readOffset_==s.frames){s.state.store(empty,std::memory_order_release);consumer_=(consumer_+1)%slotCount;readOffset_=0;}
  }
  if(done<requested)underruns_.fetch_add(1,std::memory_order_relaxed);return done;
 }
 std::uint32_t underruns()const noexcept{return underruns_.load(std::memory_order_relaxed);}
 bool resident()const noexcept{return storage_!=nullptr;}
};
}
