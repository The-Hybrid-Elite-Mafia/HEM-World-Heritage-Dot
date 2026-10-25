// SPDX-License-Identifier: GPL-3.0-only
#include "audio.h"
#include <cmath>
#include <iostream>
using namespace whic;
double rms(const OfflineRender&r){double sum=0;for(float v:r.stereo){if(!std::isfinite(v)||std::abs(v)>.98001f)throw 1;sum+=double(v)*v;}return std::sqrt(sum/r.stereo.size());}
int wmain(int n,wchar_t**v){if(n!=2)return 2;Sample s;s.path=v[1];std::wstring e;if(!probeSample(s,e)||!prepareForPlayback(s,e))return 3;s.map={60,0,127,1,127,0};s.gainDb=-24;DspParameters d;OfflineRender dry,wet,gain,chord;auto render=[&](OfflineRender&r,DspParameters p,float db,std::vector<OfflineNote> notes){return AudioEngine::renderOffline({s},L"CC0 fixture",false,notes,48000,96000,r,e,p,db);};if(!render(dry,d,0,{{0,60,100}}))return 4;d.blend=.9f;if(!render(wet,d,0,{{0,60,100}}))return 5;double ratio=rms(wet)/rms(dry);if(ratio<.5||ratio>2)return 6;d.blend=0;if(!render(gain,d,6,{{0,60,100}}))return 7;double boost=rms(gain)/rms(dry);if(boost<1.95||boost>2.01||gain.limitedFrames)return 8;s.gainDb=12;std::vector<OfflineNote> notes;for(int k=60;k<68;++k)notes.push_back({0,k,127});if(!render(chord,d,12,notes)||chord.peakVoices!=8||!chord.limitedFrames||rms(chord)<=0)return 9;std::cout<<"PASS real-recording RMS blend90/dry="<<ratio<<" output+6dB/dry="<<boost<<"; 8 distinct simultaneous voices; overload limited; finite post-output <=0.98; no device opened.\n";}
