// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "mmap_streamer.h"
#include <windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace whic {
struct MmapSampleStreamer::Impl {
 struct Page {std::array<float,PageFrames*2> pcm{};uint64_t base=UINT64_MAX;size_t count=0;};
 HANDLE file=INVALID_HANDLE_VALUE,mapping=nullptr;
 const unsigned char* view=nullptr;
 uint64_t fileBytes=0,viewBase=0,position=0;
 size_t viewLength=0;unsigned granularity=65536,align=0;
 Format format{};Stats counters{};std::array<Page,2> pages{};bool failed=false;
 ~Impl(){if(view)UnmapViewOfFile(view);if(mapping)CloseHandle(mapping);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
 bool ensureView(uint64_t offset,size_t bytes){
  if(offset>fileBytes||bytes>fileBytes-offset){failed=true;return false;}
  if(view&&offset>=viewBase&&offset-viewBase<=viewLength&&bytes<=viewLength-size_t(offset-viewBase))return true;
  if(view){UnmapViewOfFile(view);view=nullptr;viewLength=0;counters.mappedBytes=0;}
  viewBase=offset/granularity*granularity;viewLength=size_t(std::min<uint64_t>(ViewBytes,fileBytes-viewBase));
  if(offset-viewBase+bytes>viewLength){failed=true;return false;}
  view=static_cast<const unsigned char*>(MapViewOfFile(mapping,FILE_MAP_READ,DWORD(viewBase>>32),DWORD(viewBase),viewLength));
  if(!view){viewLength=0;failed=true;return false;}
  counters.mappedBytes=viewLength;counters.peakMappedBytes=std::max(counters.peakMappedBytes,viewLength);++counters.mapCount;return true;
 }
 float decode(const unsigned char* p) const {
  uint32_t raw=0;unsigned bytes=format.bits/8;
  for(unsigned j=0;j<bytes;++j)raw|=uint32_t(p[format.bigEndian?bytes-1-j:j])<<(j*8);
  if(format.floatingPoint){float value;memcpy(&value,&raw,4);return std::isfinite(value)?std::clamp(value,-1.f,1.f):0.f;}
  int64_t value=raw;if(format.bits==8&&format.unsigned8)value-=128;else if(value&(int64_t(1)<<(format.bits-1)))value-=int64_t(1)<<format.bits;
  return float(double(value)/double(int64_t(1)<<(format.bits-1)));
 }
 bool fill(Page& page,uint64_t base){
  page.base=base;page.count=0;if(base>=format.frames)return true;
  size_t count=size_t(std::min<uint64_t>(PageFrames,format.frames-base)),done=0;
  while(done<count){uint64_t offset=format.dataOffset+(base+done)*align;if(!ensureView(offset,align))return false;
   size_t available=(viewLength-size_t(offset-viewBase))/align;size_t take=std::min(count-done,available);auto* data=view+size_t(offset-viewBase);
   for(size_t j=0;j<take;++j){float left=decode(data+j*align);page.pcm[(done+j)*2]=left;page.pcm[(done+j)*2+1]=format.channels==2?decode(data+j*align+format.bits/8):left;}done+=take;
  }
  page.count=count;++counters.pageRefills;return true;
 }
 int ensurePages(){
  int active=-1;for(int j=0;j<2;++j)if(position>=pages[j].base&&position-pages[j].base<pages[j].count){active=j;break;}
  if(active<0){active=0;if(!fill(pages[0],position/PageFrames*PageFrames))return -1;pages[1].base=UINT64_MAX;pages[1].count=0;}
  uint64_t next=pages[active].base+pages[active].count;
  // Prefetch/decode the next page on this same background thread. No mapped
  // memory is referenced by the output callback, which receives copied PCM.
  if(pages[1-active].base!=next&&!fill(pages[1-active],next))return -1;
  return active;
 }
};
MmapSampleStreamer::MmapSampleStreamer():impl(std::make_unique<Impl>()){}
MmapSampleStreamer::~MmapSampleStreamer()=default;
bool MmapSampleStreamer::open(const std::wstring&path,const Format&format,std::wstring&error){
 impl=std::make_unique<Impl>();auto&i=*impl;i.format=format;
 if(format.channels<1||format.channels>2||(format.bits!=8&&format.bits!=16&&format.bits!=24&&format.bits!=32)||(format.floatingPoint&&format.bits!=32)||!format.frames){error=L"Invalid memory-mapped sample format.";return false;}
 i.align=format.channels*(format.bits/8);
 if(format.frames>(UINT64_MAX-format.dataOffset)/i.align){error=L"Sample data size overflows.";return false;}
 // Deny concurrent writes/deletion while the mapped decoder owns the source.
 i.file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(i.file==INVALID_HANDLE_VALUE){error=L"Cannot open read-only sample mapping (source may be in use for writing).";return false;}
 LARGE_INTEGER size{};if(!GetFileSizeEx(i.file,&size)||size.QuadPart<0){error=L"Cannot determine mapped sample size.";return false;}i.fileBytes=uint64_t(size.QuadPart);
 if(format.dataOffset>i.fileBytes||format.frames*i.align>i.fileBytes-format.dataOffset){error=L"Sample data exceeds mapped file bounds.";return false;}
 SYSTEM_INFO system{};GetSystemInfo(&system);i.granularity=system.dwAllocationGranularity;
 if(!i.granularity||i.granularity>ViewBytes){error=L"Unsupported Windows mapping granularity.";return false;}
 i.mapping=CreateFileMappingW(i.file,nullptr,PAGE_READONLY,0,0,nullptr);if(!i.mapping){error=L"Cannot create read-only sample mapping.";return false;}
 if(i.ensurePages()<0){error=L"Cannot map and decode initial sample pages.";return false;}return true;
}
bool MmapSampleStreamer::seek(uint64_t frame){auto&i=*impl;if(!isOpen()||frame>i.format.frames)return false;i.position=frame;return frame==i.format.frames||i.ensurePages()>=0;}
size_t MmapSampleStreamer::read(float*out,size_t count){auto&i=*impl;if(!out||!isOpen())return 0;size_t done=0;while(done<count&&i.position<i.format.frames){int active=i.ensurePages();if(active<0)break;const auto&p=i.pages[active];size_t offset=size_t(i.position-p.base),take=std::min(count-done,p.count-offset);memcpy(out+done*2,p.pcm.data()+offset*2,take*2*sizeof(float));done+=take;i.position+=take;}i.counters.copiedFrames+=done;return done;}
bool MmapSampleStreamer::isOpen() const{return impl->mapping&&!impl->failed;}
MmapSampleStreamer::Stats MmapSampleStreamer::stats() const{return impl->counters;}
}
