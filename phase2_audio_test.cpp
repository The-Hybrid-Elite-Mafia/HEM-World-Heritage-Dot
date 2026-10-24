// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "audio.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
namespace {
struct Stats {double rms=0,peak=0;bool finite=true;};
Stats stats(const std::vector<float>& samples,size_t start=0){Stats s;double sum=0;for(size_t i=start;i<samples.size();++i){s.finite=s.finite&&std::isfinite(samples[i]);s.peak=std::max(s.peak,std::abs(double(samples[i])));sum+=double(samples[i])*samples[i];}if(start<samples.size())s.rms=std::sqrt(sum/(samples.size()-start));return s;}
double difference(const std::vector<float>& a,const std::vector<float>& b){double sum=0;for(size_t i=0;i<a.size();++i){double d=double(a[i])-b[i];sum+=d*d;}return std::sqrt(sum/a.size());}
}
int wmain(int argc,wchar_t** argv){
 if(argc!=2){std::wcerr<<L"Usage: whic_phase2_audio_test <real-acoustic-preset.whic>\n";return 2;}
 std::vector<whic::Sample> samples;std::wstring provenance,error;if(!whic::loadPreset(argv[1],samples,provenance,error)||samples.empty()){std::wcerr<<error;return 3;}for(auto& s:samples)if(!whic::prepareForPlayback(s,error)){std::wcerr<<error;return 4;}
 // Device startup is deliberately excluded from this silent DSP test.
 auto sample=samples.front();sample.map.low=0;sample.map.high=127;sample.map.velLow=1;sample.map.velHigh=127;std::vector<whic::OfflineNote> events{{0,sample.map.root,100},{24000,sample.map.root,0}};
 constexpr unsigned frames=240000;whic::OfflineRender neutral;
 if(!whic::AudioEngine::renderOffline({sample},provenance,true,events,48000,frames,neutral,error)){std::wcerr<<error;return 6;}auto baseline=stats(neutral.stereo);if(!baseline.finite||baseline.rms<1e-6){std::wcerr<<L"Real source baseline invalid\n";return 7;}
 const std::array<whic::DspParameters,5> settings{{{.9f,0,1,0},{0,-.8f,1,0},{0,0,1.7f,0},{0,0,1,.9f},{1,1,2,1}}};
 const wchar_t* names[]={L"resonance",L"tension tone filter",L"strike intensity",L"sample-derived friction",L"combined extremes"};
 for(size_t i=0;i<settings.size();++i){whic::OfflineRender effected;if(!whic::AudioEngine::renderOffline({sample},provenance,true,events,48000,frames,effected,error,settings[i])){std::wcerr<<error;return 8;}auto s=stats(effected.stereo),tail=stats(effected.stereo,192000*2);double delta=difference(neutral.stereo,effected.stereo);if(!s.finite||s.peak>1.000001||delta<1e-7||tail.peak>1e-5||effected.decodeErrors){std::wcerr<<L"FAIL DSP "<<names[i]<<L" delta="<<delta<<L" tail="<<tail.peak<<L"\n";return 9;}std::wcout<<L"PASS "<<names[i]<<L": RMS="<<s.rms<<L" delta-RMS="<<delta<<L" peak="<<s.peak<<L" tail-peak="<<tail.peak<<L" finite=true\n";}
 whic::OfflineRender silence;if(!whic::AudioEngine::renderOffline({},L"",false,{},48000,48000,silence,error,{1,1,2,1})||stats(silence.stereo).peak!=0){std::wcerr<<L"FAIL: DSP produced sound without recorded input\n";return 10;}
 float nan=std::numeric_limits<float>::quiet_NaN();whic::OfflineRender sanitized;if(!whic::AudioEngine::renderOffline({sample},provenance,true,events,48000,frames,sanitized,error,{nan,nan,nan,nan})||difference(neutral.stereo,sanitized.stereo)!=0){std::wcerr<<L"Nonfinite controls did not sanitize to neutral\n";return 11;}
 std::wcout<<L"PASS: no-input remains exact zero; invalid controls sanitize to neutral; default strike1 and other controls0 preserve sample path.\n"<<L"No audio device was opened. PCM remained in memory. Tension changes tone only; no physical instrument is synthesized. WASAPI startup is outside this silent test.\n";return 0;
}

