// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#include "asio_contract.h"
#include "asio_driver_bypass.h"
#include "realtime_handoff.h"
#include <iostream>
#include <limits>
#include <array>
#include <thread>
#include <chrono>
#include <type_traits>
using namespace whic::device_contract;
int main(){int failures=0,checks=0;auto check=[&](bool b,const char*s){++checks;if(!b){++failures;std::cerr<<"FAIL "<<s<<'\n';}};
 check(Availability{}.gate()==Gate::license_required,"license first");check(Availability{true,false,true}.gate()==Gate::sdk_unavailable,"missing SDK");check(Availability{true,true,false}.gate()==Gate::no_driver,"missing driver");
 BufferLimits powers{32,2048,256,-1},fixed{256,256,256,0},steps{48,240,96,48};
 check(accepts(powers,256)&&!accepts(powers,192),"power2 buffers");check(!accepts(powers,16)&&!accepts(powers,4096),"buffer bounds");check(accepts(fixed,256)&&!accepts(fixed,128),"fixed buffers");check(accepts(steps,144)&&!accepts(steps,145),"step buffers");check(!accepts({0,0,0,0},0),"zero rejected");check(!accepts({64,128,64,-2},64),"invalid granularity");
 Configuration c{48000,256,0,1,2,SampleType::float32_le};check(valid(c,powers),"valid config");auto bad=c;bad.right_channel=0;check(!valid(bad,powers),"duplicate channels");bad=c;bad.sample_rate=std::numeric_limits<double>::infinity();check(!valid(bad,powers),"nonfinite rate");bad=c;bad.right_channel=2;check(!valid(bad,powers),"channel bound");bad=c;bad.type=SampleType::unsupported;check(!valid(bad,powers),"unsupported type");
 Lifecycle l;check(!l.start(),"cannot start closed");check(!l.configure({},c,powers),"unlicensed configure rejected");check(l.configure({true,true,true},c,powers),"configure ready");check(l.start(),"start configured");check(!l.configure({true,true,true},c,powers),"cannot reconfigure while running");check(!l.close(true),"cannot close running");check(l.stop(),"stop running");check(!l.close(false),"requires callback quiescence");check(l.close(true),"safe close");l.fault();check(!l.start(),"fault requires recovery");check(l.close(true),"fault recovery close");
 ControlMailbox mailbox;mailbox.signal(Event::reset);mailbox.signal(Event::rate_changed);check(mailbox.take()==3,"coalesced control events");check(mailbox.take()==0,"consumed event state");check(std::abs(Latency{256,128,48000}.estimated_output_ms()-8)<1e-9,"latency estimate includes app queue");check(std::isnan(Latency{}.estimated_output_ms()),"unknown latency not zero");
 whic::AsioDriverBypass unavailable;check(!unavailable.available()&&!unavailable.start()&&!unavailable.error().empty(),"absent ASIO binding honestly unavailable");
 struct TestBinding:whic::AuthorizedAsioBinding{int prepares=0,starts=0,stops=0,disposals=0;bool failStart=false;Availability availability()const noexcept override{return {true,true,true};}BufferLimits bufferLimits()const noexcept override{return {32,2048,256,-1};}bool prepare(const Configuration&,Renderer,std::wstring&)override{++prepares;return true;}bool start(std::wstring&)override{++starts;return !failStart;}void stopAndQuiesce()noexcept override{++stops;}void dispose()noexcept override{++disposals;}};
 TestBinding fake;{whic::AsioDriverBypass wrapper(&fake);Renderer renderer{nullptr,[](void*,float*,float*,std::uint32_t)noexcept{}};check(wrapper.prepare(c,renderer)&&wrapper.start(),"wrapper delegates to test binding");check(!wrapper.prepare(c,renderer),"wrapper disallows active reprepare");wrapper.stop();check(wrapper.state()==State::stopped,"wrapper stop quiesces");wrapper.close();check(fake.prepares==1&&fake.starts==1&&fake.stops>=1&&fake.disposals==1,"wrapper disposal ownership");check(wrapper.prepare(c,renderer),"wrapper can reopen");fake.failStart=true;check(!wrapper.start()&&wrapper.state()==State::faulted,"start failure quiesces and faults");}
 whic::ResidentDoubleBuffer buffers;std::wstring error;bool resident=buffers.initialize(error);check(resident,"VirtualAlloc/VirtualLock resident control-thread setup");
 if(!resident)std::wcerr<<error<<L'\n';
 if(resident){std::array<float,4096> out{};check(buffers.consume(out.data(),32)==0&&out[0]==0&&buffers.underruns()==1,"empty consumer zero fills");auto first=buffers.producerClaim();check(bool(first),"claim first slot");std::fill_n(first.samples,512,1.f);check(!buffers.producerPublish(first,2049),"oversized publish rejected");check(buffers.producerPublish(first,256),"publish first slot");auto second=buffers.producerClaim();std::fill_n(second.samples,512,2.f);check(buffers.producerPublish(second,256),"publish second slot");check(!buffers.producerClaim(),"full slots backpressure");check(buffers.consume(out.data(),128)==128&&out[0]==1,"partial read");check(!buffers.producerClaim(),"partial slot cannot be overwritten");check(buffers.consume(out.data(),256)==256&&out[0]==1&&out[256]==2,"read crosses two slots in order");auto third=buffers.producerClaim();check(bool(third)&&buffers.producerCancel(third),"freed slot reusable and cancellable");check(buffers.consume(out.data(),256)==128&&out[0]==2&&out[256]==0,"tail zero fills underrun");buffers.close();check(!buffers.resident(),"quiescent release");check(buffers.initialize(error),"reinitialize cleared slots");
  static_assert(!std::is_move_constructible_v<whic::ResidentDoubleBuffer::ProducerLease>);
  static_assert(!std::is_nothrow_constructible_v<whic::AsioDriverBypass>);
  try{auto lease=buffers.producerLease();check(bool(lease),"lease acquired before deliberate exception");throw 7;}catch(int){}
  {auto lease=buffers.producerLease();check(bool(lease),"exception releases unpublished slot");check(!lease.publish(2049)&&bool(lease),"invalid publish retains lease ownership");}
  {auto lease=buffers.producerLease();check(bool(lease),"failed publish destructor releases slot");std::fill_n(lease.samples(),64,9.f);check(lease.publish(32)&&!lease,"successful publish relinquishes lease");}
  check(buffers.consume(out.data(),32)==32&&out[0]==9.f,"published lease survives destructor");
  {auto lease=buffers.producerLease();check(bool(lease)&&buffers.resident(),"metadata and storage live throughout lease");}
  buffers.close();check(!buffers.resident(),"close after last lease releases residency");check(buffers.initialize(error),"reinitialize after lease lifetime");
  // Test-only contention harness: producer may yield; callback method never waits.
  constexpr unsigned blocks=5000;std::atomic<bool> producerDone=false;std::atomic<unsigned> received=0,badValues=0;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);std::thread producer([&]{for(unsigned n=1;n<=blocks;){if(std::chrono::steady_clock::now()>deadline)break;auto claim=buffers.producerClaim();if(!claim){std::this_thread::yield();continue;}std::fill_n(claim.samples,128,float(n));if(buffers.producerPublish(claim,64))++n;}producerDone.store(true);});
  unsigned expected=1;while((!producerDone.load()||received.load()<blocks)&&std::chrono::steady_clock::now()<deadline){auto count=buffers.consume(out.data(),64);if(count){if(count!=64)++badValues;for(unsigned i=0;i<count*2;++i)if(out[i]!=float(expected))++badValues;++expected;++received;}else std::this_thread::yield();}producer.join();check(received==blocks&&badValues==0,"5000 concurrent handoffs preserve exact ownership and order");buffers.close();
 }
 std::cout<<checks<<" silent internal-contract checks; "<<failures<<" failures. NOT an ASIO integration test.\n";return failures?1:0;
}
