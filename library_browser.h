// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "library_names.h"
#include <cwctype>
namespace whic {
inline std::wstring lower(std::wstring s){std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});return s;}
inline const wchar_t* categories[]={L"All families",L"Winds",L"Strings",L"Percussion",L"Other / user"};
inline int libraryCategory(const std::wstring& title){auto s=lower(title);if(s.find(L"aerophones")!=s.npos||s.find(L"woodwinds")!=s.npos||s.find(L"brass")!=s.npos)return 1;if(s.find(L"chordophones")!=s.npos||s.find(L"strings")!=s.npos||s.find(L"ganjo")!=s.npos||s.find(L"guitar")!=s.npos)return 2;if(s.find(L"idiophones")!=s.npos||s.find(L"membranophones")!=s.npos||s.find(L"percussion")!=s.npos||s.find(L"tabla")!=s.npos)return 3;return 4;}
inline bool libraryMatch(const std::wstring& title,const std::wstring& query,int category){return (!category||libraryCategory(title)==category)&&(query.empty()||lower(title+L" "+instrumentDisplayName(title)).find(lower(query))!=std::wstring::npos);}
inline int wheelPage(int delta,int& remainder){remainder+=delta;int pages=remainder/120;remainder%=120;return -pages*5;}
}
