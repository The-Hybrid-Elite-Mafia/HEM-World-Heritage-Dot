// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
namespace whic {
// Worker/offline-thread only. Calls can perform mapping I/O and incur page faults.
// read() copies decoded PCM into caller-owned storage; no mapped pointer escapes.
class MmapSampleStreamer final {
public:
 static constexpr size_t ViewBytes=1024*1024;
 static constexpr size_t PageFrames=8192;
 static constexpr size_t DecodedBytes=2*PageFrames*2*sizeof(float);
 struct Format {
  uint64_t dataOffset=0,frames=0;
  unsigned channels=0,bits=0;
  bool bigEndian=false,floatingPoint=false,unsigned8=false;
 };
 struct Stats { size_t mappedBytes=0,peakMappedBytes=0;uint64_t mapCount=0,pageRefills=0,copiedFrames=0; };
 MmapSampleStreamer();
 ~MmapSampleStreamer();
 MmapSampleStreamer(const MmapSampleStreamer&)=delete;
 MmapSampleStreamer& operator=(const MmapSampleStreamer&)=delete;
 bool open(const std::wstring& path,const Format&,std::wstring& error);
 bool seek(uint64_t frame);
 size_t read(float* interleavedStereo,size_t frames);
 bool isOpen() const;
 Stats stats() const;
private:
 struct Impl;
 std::unique_ptr<Impl> impl;
};
}
