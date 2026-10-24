// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "audio.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>
#include <iomanip>
namespace fs=std::filesystem;
using namespace whic;
namespace {
std::string utf8(const std::wstring&s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string v(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),v.data(),n,nullptr,nullptr);return v;}
std::string json(const std::wstring&s){std::string r="\"";for(unsigned char c:utf8(s)){if(c=='"'||c=='\\'){r+='\\';r+=char(c);}else if(c=='\n')r+="\\n";else if(c=='\r')r+="\\r";else if(c=='\t')r+="\\t";else if(c<32)r+='?';else r+=char(c);}return r+'"';}
std::string tsv(const std::wstring&s){auto r=utf8(s);for(char&c:r)if(c=='\t'||c=='\n'||c=='\r')c=' ';return r;}
struct SourceResult{bool good=false,finite=true,nonzero=false;uint64_t frames=0;double peak=0;std::wstring error;};
SourceResult decode(const Sample&s){SourceResult r;Decoder d;if(!d.open(s,r.error))return r;std::array<float,8192> block{};while(r.frames<s.frames){auto want=size_t(std::min<uint64_t>(4096,s.frames-r.frames));auto got=d.read(block.data(),want);if(!got){r.error=L"Decoder stopped before metadata frame count";return r;}for(size_t i=0;i<got*2;++i){r.finite=r.finite&&std::isfinite(block[i]);if(std::isfinite(block[i]))r.peak=std::max(r.peak,std::abs(double(block[i])));}r.frames+=got;}if(d.read(block.data(),1)!=0){r.error=L"Decoded data exceeds metadata frame count";return r;}r.nonzero=r.peak>1e-9;r.good=r.finite&&r.nonzero;if(!r.good)r.error=r.finite?L"Source recording is entirely silent":L"Nonfinite decoded source";return r;}
struct Row{std::wstring path,error;size_t zones=0,covered=0,triggers=0;bool finite=true,nonzero=false;double peak=0;bool good=false;};
}
int wmain(int argc,wchar_t**argv){
 if(argc<2||argc>3){std::wcerr<<L"Usage: library_test <library-folder> [report-prefix]\nSilent complete source decoding and preset-mapping rendering. No audio device calls.\n";return 2;}
 std::vector<Row> rows;std::map<std::wstring,SourceResult> sources;uint64_t totalDecoded=0;unsigned failures=0;std::wstring fatal;std::error_code ec;
 fs::path root=fs::absolute(argv[1],ec),prefix=argc==3?fs::path(argv[2]):fs::path(L"library_validation");
 std::vector<fs::path> presets;
 if(ec||!fs::is_directory(root,ec)){std::wcerr<<L"Library directory is unavailable\n";return 2;}
 for(fs::recursive_directory_iterator i(root,fs::directory_options::none,ec),end;i!=end&&!ec;i.increment(ec))if(i->is_regular_file(ec)){auto ext=i->path().extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),towlower);if(ext==L".whic")presets.push_back(i->path());if(presets.size()>1024){fatal=L"Preset count exceeds validation limit1024";break;}}
 if(ec)fatal=L"Cannot enumerate complete library: "+std::to_wstring(ec.value());std::sort(presets.begin(),presets.end());if(presets.empty())fatal=L"No instrument presets found";
 if(fatal.empty())for(const auto&path:presets){Row row;row.path=fs::relative(path,root,ec).wstring();if(ec){row.path=path.wstring();ec.clear();}std::vector<Sample> pack;std::wstring provenance,error;DspParameters dsp;
  if(!loadPreset(path.wstring(),pack,provenance,error,&dsp)||pack.empty()){row.error=L"Preset load failed or empty: "+error;rows.push_back(row);++failures;continue;}row.zones=pack.size();
  if(provenance.find_first_not_of(L" \r\n\t")==std::wstring::npos)row.error=L"Missing provenance";
  for(auto&s:pack){auto canonical=fs::weakly_canonical(fs::path(s.path),ec).wstring();if(ec){canonical=s.path;ec.clear();}if(sources.find(canonical)==sources.end()){if(sources.size()>=16384){row.error=L"Unique source validation limit16384 exceeded";break;}auto r=decode(s);totalDecoded+=r.frames;sources.emplace(canonical,std::move(r));}const auto&r=sources.at(canonical);if(!r.good&&row.error.empty())row.error=L"Source failed: "+s.path+L": "+r.error;if(!prepareForPlayback(s,error)&&row.error.empty())row.error=L"Playback preparation failed: "+error;}
  if(row.error.empty())for(size_t zi=0;zi<pack.size();++zi){auto &z=pack[zi];int key=std::clamp(z.map.root,z.map.low,z.map.high),vel=(z.map.velLow+z.map.velHigh)/2;OfflineRender single;auto physical=dsp;physical.legacyMode=false;physical.blend=1;physical.resonance=.7f;
   for(auto settings:{dsp,physical}){if(!AudioEngine::renderOffline({z},provenance,false,{{0,key,vel}},48000,96000,single,error,settings)){row.error=L"Individual zone render failed "+std::to_wstring(zi)+L": "+error;break;}double energy=0;bool finite=true;for(float f:single.stereo){finite=finite&&std::isfinite(f);energy+=double(f)*f;}if(!finite||energy<=0){row.error=L"Individual zone silent/nonfinite "+std::to_wstring(zi);break;}}if(!row.error.empty())break;
  }
  if(row.error.empty()){
   // For each distinct zone root/velocity pair, replay enough times to cycle
   // through every eligible RR candidate using the actual saved preset selector.
   std::set<std::pair<int,int>> pairs;for(const auto&s:pack)pairs.emplace(std::clamp(s.map.root,s.map.low,s.map.high),(s.map.velLow+s.map.velHigh)/2);
   std::set<int> covered;
   for(auto[key,velocity]:pairs){size_t eligible=0;for(const auto&s:pack)if(key>=s.map.low&&key<=s.map.high&&velocity>=s.map.velLow&&velocity<=s.map.velHigh)++eligible;
    if(!eligible||eligible>4096){row.error=L"Invalid root/velocity eligibility";break;}if(row.triggers+eligible>65536){row.error=L"Mapping trigger bound65536 exceeded";break;}
    std::vector<OfflineNote> events;events.reserve(eligible+1);for(size_t j=0;j<eligible;++j)events.push_back({uint32_t(j*512),key,velocity});events.push_back({uint32_t(eligible*512+24000),key,0});OfflineRender result;
    if(!AudioEngine::renderOffline(pack,provenance,true,events,48000,uint32_t(eligible*512+48000),result,error,dsp)){row.error=L"Offline preset render failed: "+error;break;}
    row.triggers+=eligible;size_t onsets=0;for(const auto&selection:result.selections){if(selection.sampleIndex<0||size_t(selection.sampleIndex)>=pack.size()){row.error=L"Mapped note selected no zone";break;}const auto&m=pack[selection.sampleIndex].map;if(selection.key<m.low||selection.key>m.high||selection.velocity<m.velLow||selection.velocity>m.velHigh){row.error=L"Selector violated key/velocity mapping";break;}covered.insert(selection.sampleIndex);++onsets;}if(onsets!=eligible&&row.error.empty())row.error=L"Missing round-robin selection records";
    for(float value:result.stereo){row.finite=row.finite&&std::isfinite(value);if(std::isfinite(value))row.peak=std::max(row.peak,std::abs(double(value)));}if(!row.error.empty())break;
   }
   row.covered=covered.size();row.nonzero=row.peak>1e-9;if(row.error.empty()&&(!row.finite||!row.nonzero||row.covered!=row.zones))row.error=L"Render finite/nonzero/full-zone-coverage validation failed";
  }
  row.good=row.error.empty();if(!row.good)++failures;std::wcout<<(row.good?L"PASS ":L"FAIL ")<<row.path<<L" zones="<<row.zones<<L" covered="<<row.covered<<L" triggers="<<row.triggers<<L" peak="<<row.peak;if(!row.good)std::wcout<<L" error="<<row.error;std::wcout<<L'\n';rows.push_back(std::move(row));
 }
 if(!fatal.empty()){++failures;std::wcerr<<fatal<<L'\n';}
 auto parent=prefix.parent_path();if(!parent.empty())fs::create_directories(parent,ec);std::ofstream out(fs::path(prefix.wstring()+L".json"),std::ios::binary),table(fs::path(prefix.wstring()+L".tsv"),std::ios::binary);if(!out||!table){std::wcerr<<L"Cannot write validation reports\n";return 3;}
 out<<std::setprecision(12)<<"{\n\"silent\":true,\"audio_device_calls\":0,\"presets\":"<<rows.size()<<",\"unique_sources\":"<<sources.size()<<",\"decoded_source_frames\":"<<totalDecoded<<",\"failures\":"<<failures<<",\"fatal_error\":"<<json(fatal)<<",\"instruments\":[\n";
 table<<"preset\tzones\tcovered_zones\ttriggers\tfinite\tnonzero\tpeak\tpassed\terror\n";
 for(size_t i=0;i<rows.size();++i){const auto&r=rows[i];out<<(i?",\n":"")<<"{\"preset\":"<<json(r.path)<<",\"zones\":"<<r.zones<<",\"covered_zones\":"<<r.covered<<",\"triggers\":"<<r.triggers<<",\"finite\":"<<(r.finite?"true":"false")<<",\"nonzero\":"<<(r.nonzero?"true":"false")<<",\"peak\":"<<r.peak<<",\"passed\":"<<(r.good?"true":"false")<<",\"error\":"<<json(r.error)<<"}";table<<tsv(r.path)<<'\t'<<r.zones<<'\t'<<r.covered<<'\t'<<r.triggers<<'\t'<<r.finite<<'\t'<<r.nonzero<<'\t'<<r.peak<<'\t'<<r.good<<'\t'<<tsv(r.error)<<'\n';}
 out<<"\n],\"sources\":[\n";size_t n=0;for(const auto&[path,r]:sources)out<<(n++?",\n":"")<<"{\"path\":"<<json(path)<<",\"frames\":"<<r.frames<<",\"finite\":"<<(r.finite?"true":"false")<<",\"nonzero\":"<<(r.nonzero?"true":"false")<<",\"peak\":"<<r.peak<<",\"passed\":"<<(r.good?"true":"false")<<",\"error\":"<<json(r.error)<<"}";out<<"\n]}\n";
 std::wcout<<L"SUMMARY presets="<<rows.size()<<L" unique sources="<<sources.size()<<L" decoded frames="<<totalDecoded<<L" failures="<<failures<<L". PCM remained in memory; no audio devices opened.\n";return failures?1:0;
}

