// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "mmap_streamer.h"
#include "dsp.h"
namespace whic {
struct Mapping { int root=60,low=0,high=127,velLow=1,velHigh=127,rr=0; };
struct PreparedAudio;
struct Sample { bool releaseTrigger=false; std::wstring path; Mapping map; uint32_t rate=0; uint64_t frames=0; std::shared_ptr<PreparedAudio> prepared; float releaseSeconds=0.05f;
 uint64_t startOffset=0; float tuneCents=0,gainDb=0,attackSeconds=0.003f;
};
bool validSampleParameters(const Sample&);
Mapping inferMapping(const std::wstring& path);
bool probeSample(Sample&,std::wstring& error);
bool prepareForPlayback(Sample&,std::wstring& error);
bool isPlaybackPrepared(const Sample&);
class Decoder {
public:
 Decoder(); ~Decoder();
 Decoder(const Decoder&)=delete; Decoder& operator=(const Decoder&)=delete;
 bool open(const Sample&,std::wstring&); bool usingPreparedPcm() const; bool usingMemoryMap() const; MmapSampleStreamer::Stats memoryMapStats() const; bool seek(uint64_t); size_t read(float*,size_t);
private: struct Impl; std::unique_ptr<Impl> impl;
};
bool savePreset(const std::wstring&,const std::vector<Sample>&,const std::wstring& provenance,std::wstring& error,const DspParameters& dsp={});
bool loadPreset(const std::wstring&,std::vector<Sample>&,std::wstring& provenance,std::wstring& error,DspParameters* dsp=nullptr);
}




