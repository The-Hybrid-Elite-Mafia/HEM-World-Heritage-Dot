// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include <windows.h>
#include <cstring>
#include <iostream>
#include "audio.h"
int wmain(){
 constexpr size_t bytes=size_t(whic::wasapi_policy::residentRingFrames)*2*sizeof(float);
 static_assert(bytes==131072);
 void* p=VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 if(!p){std::cout<<"FAIL VirtualAlloc error="<<GetLastError()<<'\n';return 1;}
 std::memset(p,0,bytes);
 if(!VirtualLock(p,bytes)){auto e=GetLastError();VirtualFree(p,0,MEM_RELEASE);std::cout<<"FAIL VirtualLock error="<<e<<'\n';return 2;}
 bool unlocked=VirtualUnlock(p,bytes)!=FALSE;
 bool freed=VirtualFree(p,0,MEM_RELEASE)!=FALSE;
 std::cout<<(unlocked&&freed?"PASS":"FAIL")<<" actual WASAPI ring allocation/prefault/VirtualLock/unlock/free: "<<bytes<<" bytes. No working-set quota change, audio device initialization or playback.\n";
 return unlocked&&freed?0:3;
}
