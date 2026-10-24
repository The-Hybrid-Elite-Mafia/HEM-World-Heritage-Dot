#include "audio.h"
#include <filesystem>
#include <fstream>
#include <iostream>
int wmain(int argc,wchar_t**argv){if(argc!=4)return 2;std::vector<whic::Sample> zones;std::wstring meta,e;whic::DspParameters p;if(!whic::loadPreset(argv[1],zones,meta,e,&p))return 3;for(auto&s:zones)if(!whic::prepareForPlayback(s,e))return 4;whic::OfflineRender r;if(!whic::AudioEngine::renderOffline(zones,meta,true,{{0,60,100},{1000,64,90},{8000,60,0},{12000,64,0}},48000,48000,r,e,p))return 5;std::ofstream o(std::filesystem::path(argv[2]),std::ios::binary);o.write(reinterpret_cast<const char*>(r.stereo.data()),r.stereo.size()*sizeof(float));if(!whic::savePreset(argv[3],zones,meta,e,p))return 6;return 0;}
