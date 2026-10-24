// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "audio.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <array>
#include <limits>
namespace {
constexpr uint32_t Rate=48000;
struct Stats {double rms=0,peak=0;bool finite=true;};
Stats stats(const std::vector<float>& data,size_t first=0){Stats s;double squares=0;for(size_t i=first;i<data.size();++i){s.finite=s.finite&&std::isfinite(data[i]);s.peak=std::max(s.peak,std::abs(double(data[i])));squares+=double(data[i])*data[i];}if(first<data.size())s.rms=std::sqrt(squares/(data.size()-first));return s;}
}
int wmain(int argc,wchar_t** argv){
 if(argc!=2){std::wcerr<<L"Usage: whic_real_audio_test <licensed-real-acoustic-instrument.whic>\n";return 2;}
 std::vector<whic::Sample> pack;std::wstring provenance,error;
 if(!whic::loadPreset(argv[1],pack,provenance,error)||pack.empty()){std::wcerr<<error<<L" / preset must contain real samples\n";return 3;}
 for(auto& s:pack)if(!whic::prepareForPlayback(s,error)){std::wcerr<<error<<L"\n";return 4;}
 auto render=[&](const std::vector<whic::Sample>& samples,const std::vector<whic::OfflineNote>& events,uint32_t frames,whic::OfflineRender& result){if(!whic::AudioEngine::renderOffline(samples,provenance,true,events,Rate,frames,result,error)){std::wcerr<<error<<L"\n";return false;}if(!stats(result.stereo).finite){std::wcerr<<L"Nonfinite sample output\n";return false;}return true;};
 // Exercise the actual saved preset's key/velocity selection, using one event per zone.
 std::vector<whic::OfflineNote> mappedEvents;size_t tested=std::min<size_t>(pack.size(),128);
 for(size_t i=0;i<tested;++i){const auto& m=pack[i].map;int key=std::clamp(m.root,m.low,m.high);mappedEvents.push_back({uint32_t(i*10000),key,(m.velLow+m.velHigh)/2});mappedEvents.push_back({uint32_t(i*10000+6000),key,0});}
 whic::OfflineRender mapped;if(!render(pack,mappedEvents,uint32_t(tested*10000+5000),mapped))return 5;
 for(const auto& selection:mapped.selections){if(selection.sampleIndex<0||size_t(selection.sampleIndex)>=pack.size()){std::wcerr<<L"Mapped event selected no sample\n";return 6;}const auto& m=pack[selection.sampleIndex].map;if(selection.key<m.low||selection.key>m.high||selection.velocity<m.velLow||selection.velocity>m.velHigh){std::wcerr<<L"Key/velocity selection violated saved mapping\n";return 7;}}
 auto mapStats=stats(mapped.stereo);if(mapStats.rms<1e-6){std::wcerr<<L"Real preset render was silent\n";return 8;}
 std::wcout<<L"PASS saved real preset: "<<pack.size()<<L" zones; "<<mapped.selections.size()<<L" key/velocity selections; RMS="<<mapStats.rms<<L" peak="<<mapStats.peak<<L" finite=true\n";
 // Isolate one unchanged real recording to verify interpolation and pitch mathematically.
 whic::Sample one=pack.front();one.map.low=0;one.map.high=127;one.map.velLow=1;one.map.velHigh=127;one.map.rr=0;
 int root=one.map.root,pitch=root<=120?root+7:root-7;
 uint32_t releaseFrames=uint32_t(std::ceil(one.releaseSeconds*Rate))+512,releaseEnd=12000+releaseFrames;
 whic::OfflineRender base,transposed;if(!render({one},{{0,root,100},{12000,root,0}},releaseEnd+4096,base)||!render({one},{{0,pitch,100},{12000,pitch,0}},releaseEnd+4096,transposed))return 9;
 auto baseStats=stats(base.stereo),pitchStats=stats(transposed.stereo),tailStats=stats(transposed.stereo,size_t(releaseEnd)*2);
 if(baseStats.rms<1e-6||pitchStats.rms<1e-6||tailStats.peak!=0){std::wcerr<<L"Real source pitch/release check failed\n";return 10;}
 double step=double(one.rate)/Rate*std::pow(2.0,(pitch-root)/12.0);size_t expectedFrames=size_t(std::ceil((releaseEnd+10)*step))+2;std::vector<float> source(expectedFrames*2);whic::Decoder decoder;if(!decoder.open(one,error)){std::wcerr<<error;return 11;}size_t got=decoder.read(source.data(),expectedFrames);double worst=0;float envelope=0;double position=0;size_t compared=0;
 for(size_t frame=0;frame<releaseEnd;++frame){size_t a=size_t(position);if(a+1>=got)break;float frac=float(position-a);if(frame<12000)envelope=std::min(1.f,envelope+1.f/(.003f*Rate));else envelope=std::max(0.f,envelope-1.f/(one.releaseSeconds*Rate));float gain=.25f*100/127.f;for(size_t ch=0;ch<2;++ch){float expected=(source[a*2+ch]+(source[(a+1)*2+ch]-source[a*2+ch])*frac)*gain*envelope;worst=std::max(worst,std::abs(double(expected)-transposed.stereo[frame*2+ch]));}position+=step;++compared;}
 if(compared<2000||worst>1e-6){std::wcerr<<L"Pitch interpolation reference mismatch: "<<worst<<L" compared="<<compared<<L"\n";return 12;}
 std::wcout<<L"PASS real-recording pitch + release: root="<<root<<L" audition="<<pitch<<L" release-seconds="<<one.releaseSeconds<<L" source-step="<<step<<L" compared="<<compared<<L" max-error="<<worst<<L" base-RMS="<<baseStats.rms<<L" pitch-RMS="<<pitchStats.rms<<L" released-tail-peak="<<tailStats.peak<<L"\n";
 // Controlled mapping regression uses only the pack's actual recorded samples.
 auto alternate=pack.size()>1?pack[1]:one;alternate.map=one.map;one.map.rr=0;alternate.map.rr=1;
 whic::OfflineRender rr;if(!render({one,alternate},{{0,root,100},{4000,root,0},{8000,root,100},{12000,root,0}},18000,rr)||rr.selections.size()!=2||rr.selections[0].sampleIndex!=0||rr.selections[1].sampleIndex!=1){std::wcerr<<L"Round-robin selection failed\n";return 13;}
 one.map.velHigh=63;alternate.map.velLow=64;whic::OfflineRender velocity;if(!render({one,alternate},{{0,root,40},{4000,root,0},{8000,root,100},{12000,root,0}},18000,velocity)||velocity.selections.size()!=2||velocity.selections[0].sampleIndex!=0||velocity.selections[1].sampleIndex!=1){std::wcerr<<L"Velocity layer selection failed\n";return 14;}
 std::wcout<<L"PASS recorded-sample round-robin order0,1 and velocity layers40->0 /100->1\n";
 one.map.velHigh=127;std::vector<whic::OfflineNote> chord;for(int key=40;key<80;++key)chord.push_back({0,key,90});for(int key=40;key<80;++key)chord.push_back({4000,key,0});whic::OfflineRender dense;
 if(!render({one},chord,4000+releaseFrames+4096,dense)||dense.peakVoices!=32||dense.voiceSteals!=8||dense.decodeErrors!=0||stats(dense.stereo).rms<1e-6||stats(dense.stereo,size_t(4000+releaseFrames)*2).peak!=0){std::wcerr<<L"32-voice cap/steal/release regression failed\n";return 15;}
 std::wcout<<L"PASS real sample 40-note onset: peak-voices="<<dense.peakVoices<<L" steals="<<dense.voiceSteals<<L" decode-errors="<<dense.decodeErrors<<L" RMS="<<stats(dense.stereo).rms<<L" tail=0\n";
 std::wcout<<L"ALL PASS. Offline PCM remained in memory. No WASAPI/MIDI device was opened and no audible output was produced.\n";return 0;
}

