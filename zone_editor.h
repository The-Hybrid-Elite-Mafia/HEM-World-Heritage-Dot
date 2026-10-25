// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sample.h"
#include <array>
#include <string>
namespace whic {
inline std::wstring noteLabel(int n){const wchar_t* names[]={L"C",L"C#",L"D",L"D#",L"E",L"F",L"F#",L"G",L"G#",L"A",L"A#",L"B"};return n>=0&&n<128?std::wstring(names[n%12])+std::to_wstring(n/12-1)+L" / MIDI "+std::to_wstring(n):L"MIDI 0-127 required";}
struct ZoneDraft {std::array<std::wstring,6> values;bool release=false;int focus=0;bool replace=true;
 void begin(const Sample&s){auto m=s.map;int v[]={m.root,m.low,m.high,m.velLow,m.velHigh,m.rr};for(int i=0;i<6;++i)values[i]=std::to_wstring(v[i]);release=s.releaseTrigger;focus=0;replace=true;}
 bool parse(Mapping& m,std::wstring& error)const{int v[6];for(int i=0;i<6;++i){auto&s=values[i];if(s.empty()||s.size()>3||s.find_first_not_of(L"0123456789")!=s.npos){error=L"Each mapping field must contain a whole number.";return false;}v[i]=std::stoi(s);if(v[i]>(i==5?127:127)||v[i]<(i==3||i==4?1:0)){error=L"Notes and RR: 0-127. Velocity: 1-127.";return false;}}if(v[1]>v[2]){error=L"Low key must be at or below high key.";return false;}if(v[3]>v[4]){error=L"Low velocity must be at or below high velocity.";return false;}m={v[0],v[1],v[2],v[3],v[4],v[5]};error.clear();return true;}
 void type(wchar_t c){auto&s=values[focus];if(c==8){if(!s.empty())s.pop_back();replace=false;}else if(c>=L'0'&&c<=L'9'){if(replace)s.clear();replace=false;if(s.size()<3)s+=c;}}
};
}
