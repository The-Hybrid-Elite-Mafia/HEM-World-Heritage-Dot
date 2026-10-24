#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
namespace whic {
struct AudioPreferences { std::string backend="wasapi-shared", driver, endpoint; };
inline bool validPreferences(const AudioPreferences& p){ if(p.backend!="asio"&&p.backend!="wasapi-shared"&&p.backend!="wasapi-exclusive")return false; if(p.driver.size()>128||p.endpoint.size()>1024)return false; if(p.backend=="asio"){ std::wstring w(p.driver.begin(),p.driver.end()); CLSID id{}; if(FAILED(CLSIDFromString(w.c_str(),&id)))return false;} return true; }
inline bool loadPreferences(const std::filesystem::path& path,AudioPreferences& out,std::wstring& error){std::error_code ec;if(!std::filesystem::exists(path,ec)&&!ec){out={};error.clear();return true;}std::ifstream f(path);std::string magic,extra;AudioPreferences p;if(!(f>>magic>>std::quoted(p.backend)>>std::quoted(p.driver)>>std::quoted(p.endpoint))||magic!="WHIC_AUDIO_1"||(f>>extra)||!validPreferences(p)){error=L"Invalid audio preferences; audio remains stopped. No fallback.";return false;}out=p;error.clear();return true;}
inline bool savePreferences(const std::filesystem::path& path,const AudioPreferences& p,std::wstring& error){if(!validPreferences(p)){error=L"Invalid audio preferences; not saved.";return false;}auto temp=path;temp+=L".tmp";{std::ofstream f(temp,std::ios::trunc);f<<"WHIC_AUDIO_1\n"<<std::quoted(p.backend)<<'\n'<<std::quoted(p.driver)<<'\n'<<std::quoted(p.endpoint)<<'\n';f.close();if(!f){error=L"Cannot write audio preferences.";return false;}}if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){error=L"Cannot commit audio preferences.";return false;}error.clear();return true;}
inline std::filesystem::path preferencesPath(){wchar_t p[32768]{};GetModuleFileNameW(nullptr,p,32768);return std::filesystem::path(p).parent_path()/L"audio-preferences.conf";}
}
