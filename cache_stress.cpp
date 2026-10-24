// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "audio.h"
#include <filesystem>
#include <fstream>
#include <array>
#include <vector>
#include <memory>
#include <chrono>
#include <iostream>
#include <cmath>
#include <cstdint>
// Silent decoder/cache benchmark. Never starts WASAPI, MIDI or any playback device.
namespace {
uint64_t fingerprint(const std::wstring& path){std::ifstream f(std::filesystem::path(path),std::ios::binary);uint64_t hash=14695981039346656037ull;std::array<char,8192> bytes{};while(f){f.read(bytes.data(),bytes.size());for(std::streamsize i=0;i<f.gcount();++i){hash^=static_cast<unsigned char>(bytes[size_t(i)]);hash*=1099511628211ull;}}return hash;}
double ms(std::chrono::steady_clock::time_point start){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
}
int wmain(int argc,wchar_t** argv){
 if(argc!=2){std::wcerr<<L"Usage: whic_cache_stress <TEST-ONLY FLAC fixture>\n";return 2;}
 whic::Sample sample;sample.path=argv[1];std::wstring error;auto before=fingerprint(sample.path);
 if(!whic::probeSample(sample,error)){std::wcerr<<error<<L"\n";return 3;}
 if(whic::isPlaybackPrepared(sample)){std::wcerr<<L"Expected an unprepared FLAC test fixture.\n";return 4;}
 whic::AudioEngine engine;
 if(engine.setSamples({sample},error,L"SYNTHETIC TEST FIXTURE ONLY - silent cache validation",true)){std::wcerr<<L"FAIL: engine accepted unprepared FLAC\n";return 5;}
 std::wcout<<L"PASS: engine rejects unprepared FLAC before playback publication.\n";
 constexpr size_t count=2049;std::array<float,count*2> reference{};size_t referenceFrames=0;
 {whic::Decoder original;if(!original.open(sample,error)){std::wcerr<<error<<L"\n";return 6;}referenceFrames=original.read(reference.data(),count);}
 if(!referenceFrames){std::wcerr<<L"No reference audio decoded\n";return 7;}
 auto prepareStart=std::chrono::steady_clock::now();if(!whic::prepareForPlayback(sample,error)){std::wcerr<<error<<L"\n";return 8;}double preparationMs=ms(prepareStart);
 if(!whic::isPlaybackPrepared(sample)){std::wcerr<<L"Preparation did not publish usable PCM cache\n";return 9;}
  if(engine.setSamples({sample},error,L"",true)){std::wcerr<<L"FAIL: blank provenance accepted\n";return 16;}
 if(engine.setSamples({sample},error,L"SYNTHETIC TEST FIXTURE ONLY - silent cache validation",false)){std::wcerr<<L"FAIL: missing acoustic confirmation accepted\n";return 17;}
 std::wcout<<L"PASS: nonempty samples rejected with blank provenance or missing source confirmation.\n";if(!engine.setSamples({sample},error,L"SYNTHETIC TEST FIXTURE ONLY - silent cache validation",true)){std::wcerr<<error<<L"\n";return 10;}
 // Keep 32 decoder handles live concurrently, matching the voice cap.
 std::array<std::unique_ptr<whic::Decoder>,32> decoders;
 std::array<float,count*2> decoded{};double largestError=0;auto onsetStart=std::chrono::steady_clock::now();
 for(auto& d:decoders){d=std::make_unique<whic::Decoder>();if(!d->open(sample,error)){std::wcerr<<error<<L"\n";return 11;}if(!d->usingPreparedPcm()){std::wcerr<<L"FAIL: voice decoder did not select native prepared PCM backend\n";return 15;}auto got=d->read(decoded.data(),count);if(got!=referenceFrames){std::wcerr<<L"Cached frame count mismatch\n";return 12;}for(size_t i=0;i<got*2;++i){largestError=std::max(largestError,std::abs(double(decoded[i])-reference[i]));}}
 double onsetMs=ms(onsetStart);
 double aheadMs=1024.0/44100.0*1000.0;std::wcout<<L"32-start cost / 1024-frame ahead window at44100Hz: "<<onsetMs<<L" / "<<aheadMs<<L" ms ("<<(onsetMs/aheadMs)<<L"x). OS cache state is uncontrolled; this is not a cold-disk guarantee.\n";
 if(largestError>1e-6){std::wcerr<<L"Cached PCM differs from decoded source: "<<largestError<<L"\n";return 13;}
 if(fingerprint(sample.path)!=before){std::wcerr<<L"FAIL: original FLAC bytes changed\n";return 14;}
 std::wcout<<L"PASS: prepared cache accepted; all32 decoders use native prepared PCM (no MF voice decoder); 32 concurrent decoder opens + "<<referenceFrames<<L" stereo frames each = "<<onsetMs<<L" ms.\n"<<L"One-time preparation="<<preparationMs<<L" ms; maximum PCM/reference error="<<largestError<<L"; original source bytes unchanged.\n"<<L"SILENT BENCHMARK ONLY: no WASAPI start, MIDI input, or audible playback; not a glitch-free playback certification.\n";
 return 0;
}


