// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#pragma once
#include "asio_contract.h"
#include <string>

namespace whic {
// This is our application boundary, NOT Steinberg's driver ABI. A separately
// authorized SDK translation unit must implement this interface. No backend is
// supplied here, and the absence of one is a hard, visible unavailable state.
class AuthorizedAsioBinding {
public:
 virtual ~AuthorizedAsioBinding()=default;
 virtual device_contract::Availability availability() const noexcept=0;
 virtual device_contract::BufferLimits bufferLimits()const noexcept=0;
 virtual bool prepare(const device_contract::Configuration&,device_contract::Renderer,std::wstring& error)=0;
 virtual bool start(std::wstring& error)=0;
 // Must stop driver and synchronously quiesce every callback before returning.
 virtual void stopAndQuiesce() noexcept=0;
 virtual void dispose() noexcept=0;
};

// Every method, including lifecycle/status access, is control-thread-only.
// External binding owner must outlive this adapter. close()/destructor call the
// binding's synchronous stopAndQuiesce before dispose; no callback may retain
// references to disposed buffers or this adapter after that call returns.
class AsioDriverBypass {
 AuthorizedAsioBinding* binding_=nullptr; // external owner outlives this object
 device_contract::Lifecycle lifecycle_;
 std::wstring error_=L"ASIO unavailable: no authorized SDK binding supplied.";
 bool prepared_=false;
public:
 explicit AsioDriverBypass(AuthorizedAsioBinding* binding=nullptr):binding_(binding){}
 AsioDriverBypass(const AsioDriverBypass&)=delete;
 AsioDriverBypass& operator=(const AsioDriverBypass&)=delete;
 ~AsioDriverBypass(){close();} // control thread only; binding must still exist
 bool prepare(device_contract::Configuration config,device_contract::Renderer renderer){
  using namespace device_contract;
  if(!binding_){error_=L"ASIO unavailable: no authorized SDK binding supplied.";return false;}
  if(prepared_){error_=L"Close the current driver before preparing another configuration.";return false;}
  auto gate=binding_->availability().gate();
  if(gate!=Gate::ready){error_=gate==Gate::license_required?L"ASIO license authorization is required.":gate==Gate::sdk_unavailable?L"ASIO SDK adapter has not been built.":L"No compatible installed ASIO driver is available.";return false;}
  if(!renderer.render||!valid(config,binding_->bufferLimits())){error_=L"Invalid render callback, channel mapping, sample rate or buffer size.";return false;}
  if(!binding_->prepare(config,renderer,error_)){binding_->stopAndQuiesce();binding_->dispose();return false;}
  if(!lifecycle_.configure(binding_->availability(),config,binding_->bufferLimits())){binding_->stopAndQuiesce();binding_->dispose();error_=L"Driver configuration changed during preparation.";return false;}
  prepared_=true;error_.clear();return true;
 }
 bool start(){
  if(!prepared_||!binding_){error_=L"ASIO has not been prepared.";return false;}
  if(lifecycle_.state()!=device_contract::State::configured&&lifecycle_.state()!=device_contract::State::stopped){error_=L"ASIO is already running or requires recovery.";return false;}
  if(!binding_->start(error_)){binding_->stopAndQuiesce();lifecycle_.fault();return false;}
  return lifecycle_.start();
 }
 void stop()noexcept{if(prepared_&&binding_){binding_->stopAndQuiesce();lifecycle_.stop();}}
 void close()noexcept{if(prepared_&&binding_){binding_->stopAndQuiesce();binding_->dispose();prepared_=false;}lifecycle_.close(true);}
 const std::wstring& error()const noexcept{return error_;}
 device_contract::State state()const noexcept{return lifecycle_.state();}
 bool available()const noexcept{return binding_&&binding_->availability().gate()==device_contract::Gate::ready;}
};
}
