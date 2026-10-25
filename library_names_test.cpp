#include "library_names.h"
#include <filesystem>
#include <iostream>
int wmain(int n,wchar_t**v){if(n!=2)return 2;using whic::instrumentDisplayName;if(instrumentDisplayName(L"Aerophones - Edge-blown Aerophones - Bagpipe - Sustain")!=L"Bagpipe - Sustain")return 3;if(instrumentDisplayName(L"Woodwinds_Flute_susNV")!=L"Flute susNV")return 4;unsigned count=0;for(auto&e:std::filesystem::recursive_directory_iterator(v[1]))if(e.path().extension()==L".whic"){auto original=e.path().stem().wstring();auto name=instrumentDisplayName(original);if(name.empty()||name.rfind(L"Aerophones -",0)==0||name.rfind(L"Chordophones -",0)==0)return 5;++count;}std::cout<<"PASS instrument-first names for "<<count<<" presets; original filenames and metadata unchanged\n";return count==402?0:6;}
