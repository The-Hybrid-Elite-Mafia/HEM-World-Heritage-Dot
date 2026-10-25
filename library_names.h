#pragma once
#include <string>
#include <algorithm>
namespace whic {
inline std::wstring instrumentDisplayName(std::wstring s){
 const wchar_t* families[]={L"Aerophones",L"Chordophones",L"Idiophones",L"Membranophones"};
 for(auto f:families){std::wstring prefix=std::wstring(f)+L" - ";if(s.rfind(prefix,0)==0){auto second=s.find(L" - ",prefix.size());if(second!=std::wstring::npos)s=s.substr(second+3);break;}}
 if(s.rfind(L"VSCO_1_",0)==0)s=s.substr(7);
 for(auto p:{L"Woodwinds_",L"Brass_",L"Strings_",L"Percussion_",L"Keys_"})if(s.rfind(p,0)==0){s=s.substr(std::wstring(p).size());break;}
 for(auto p:{L"varMetal_",L"varWood_"})if(s.rfind(p,0)==0)s=s.substr(std::wstring(p).size());
 std::replace(s.begin(),s.end(),L'_',L' ');return s;
}
}
