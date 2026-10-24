// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 The-Hybrid-Elite-Mafia contributors
// No warranty; see LICENSE.
#define NOMINMAX
#include "sample.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <array>
namespace fs=std::filesystem;
int main(int argc,char**argv){
 int fail=0,total=0;auto check=[&](bool ok,const char*label){++total;std::cout<<(ok?"PASS: ":"FAIL: ")<<label<<'\n';if(!ok)++fail;};
 auto originalCwd=fs::current_path();auto fixtures=fs::absolute(argc>1?argv[1]:"test-fixtures").lexically_normal();
 auto root=fs::absolute("portable-test-output")/("portable-fixture-"+std::to_string(GetCurrentProcessId()));fs::create_directories(root/"original"/"samples");fs::create_directories(root/"original"/"presets");fs::create_directories(root/"unrelated-cwd");
 // Copy a labelled synthetic validation file only; never open audio devices.
 fs::copy_file(fixtures/"fixture_C4_v1-80_rr2.wav",root/"original"/"samples"/"fixture.wav",fs::copy_options::overwrite_existing);
 whic::Sample sample;sample.path=(root/"original"/"samples"/"fixture.wav").wstring();sample.releaseSeconds=1.5f;sample.map=whic::inferMapping(L"fixture_C4_v1-80_rr2.wav");std::wstring error,provenance;
 check(whic::probeSample(sample,error),"fixture metadata");auto sourcePath=sample.path;std::vector<whic::Sample> zones{sample};
 auto preset=root/"original"/"instrument.whic";check(whic::savePreset(preset.wstring(),zones,L"SYNTHETIC TEST FIXTURE ONLY",error),"save portable descendant path");check(zones[0].path==sourcePath,"save preserves original source path");
 auto inspectPath=[&](const fs::path&p){std::ifstream f(p,std::ios::binary);std::string magic,prov,path;int version;size_t count;f>>magic>>version>>std::quoted(prov);if(version>=3){float r,t,s,k;f>>r>>t>>s>>k;}f>>count>>std::quoted(path);return fs::path(path);};
 check(inspectPath(preset)==fs::path("samples")/"fixture.wav","stored descendant reference is relative");
 auto sibling=root/"original"/"presets"/"instrument.whic";check(whic::savePreset(sibling.wstring(),zones,L"fixture",error),"save sibling sample reference");check(inspectPath(sibling)==fs::path("..")/"samples"/"fixture.wav","stored sibling reference is relative");
 fs::copy(root/"original",root/"relocated",fs::copy_options::recursive);fs::current_path(root/"unrelated-cwd");std::vector<whic::Sample> loaded;
 check(whic::loadPreset((root/"relocated"/"instrument.whic").wstring(),loaded,provenance,error),"moved bundle loads outside its working directory");
 check(loaded.size()==1&&loaded[0].path==(root/"relocated"/"samples"/"fixture.wav").wstring(),"relative reference resolved against moved preset");
 check(loaded.size()==1&&loaded[0].releaseSeconds==1.5f,"release time survives portable v3 roundtrip");
 whic::Decoder decoder;check(!loaded.empty()&&decoder.open(loaded[0],error),"moved source decoder opens silently");std::array<float,32> pcm{};check(decoder.read(pcm.data(),16)==16,"moved source decodes silently");
 check(whic::loadPreset((root/"relocated"/"presets"/"instrument.whic").wstring(),loaded,provenance,error),"moved sibling reference loads outside CWD");
 // Old v1 absolute paths remain supported. Test paths here use ASCII fixtures.
 auto legacy=root/"legacy.whic";{std::ofstream f(legacy);f<<"WHIC_PRESET 1\n\"fixture\"\n1\n"<<std::quoted(fs::path(sourcePath).string())<<" 60 60 60 1 127 0\n";}
 check(whic::loadPreset(legacy.wstring(),loaded,provenance,error),"legacy absolute preset loads");check(loaded.size()==1&&loaded[0].path==sourcePath,"legacy absolute source stays absolute");
 check(loaded.size()==1&&loaded[0].releaseSeconds==0.05f,"legacy v1 retains default release time");
  auto legacyRelative=root/"relocated"/"legacy-relative.whic";{std::ofstream f(legacyRelative);f<<"WHIC_PRESET 1\n\"fixture\"\n1\n\"samples/fixture.wav\" 60 60 60 1 127 0\n";}
 check(whic::loadPreset(legacyRelative.wstring(),loaded,provenance,error),"v1 relative source resolves against preset");
 auto invalidZones=zones;invalidZones[0].releaseSeconds=31.f;check(!whic::savePreset(preset.wstring(),invalidZones,L"test",error),"out-of-range release rejected on save");
 auto badRelease=root/"bad-release.whic";{std::ofstream f(badRelease);f<<"WHIC_PRESET 2\n\"fixture\"\n1\n\"samples/fixture.wav\" 60 60 60 1 127 0 -1\n";}
 auto retained=loaded[0].path;check(!whic::loadPreset(badRelease.wstring(),loaded,provenance,error),"invalid v2 release rejected on load");check(loaded[0].path==retained,"invalid release load preserves current instrument");
 auto before=loaded[0].path;auto missing=root/"missing.whic";{std::ofstream f(missing);f<<"WHIC_PRESET 1\n\"fixture\"\n1\n\"missing.wav\" 60 60 60 1 127 0\n";}
 check(!whic::loadPreset(missing.wstring(),loaded,provenance,error),"missing portable source rejected");check(loaded.size()==1&&loaded[0].path==before,"failed portable load leaves current instrument unchanged");
 fs::current_path(originalCwd);std::cout<<total<<" checks; "<<fail<<" failures\n";return fail?1:0;
}





