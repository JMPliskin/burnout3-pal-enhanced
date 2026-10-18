#pragma once
// Reuses ps2host::Disc::BootPath and settings; no game data embedded here.
#include <filesystem>
#include <string>
#include <stdexcept>
#include <windows.h>
#include "ps2host/disc.h"
namespace pal_player {
inline std::string Utf8(const std::wstring& text) {
 if(text.empty())return {};
 int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
 if(!n)throw std::runtime_error("Invalid Unicode path");
 std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),n,nullptr,nullptr);return out;
}
inline std::wstring Wide(const std::string& text) {
 if(text.empty())return {};
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
 if(!n)throw std::runtime_error("Invalid UTF-8 path");
 std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),n);return out;
}
inline bool CompatibleDisc(const std::string& path) {
 ps2host::Disc disc;if(!disc.Open(path))return false;
 std::string boot=disc.BootPath();auto semicolon=boot.find(';');if(semicolon!=std::string::npos)boot.resize(semicolon);
 while(!boot.empty() && (boot[0]=='\\' || boot[0]=='/'))boot.erase(0,1);
 return _stricmp(boot.c_str(),"SLES_525.84")==0;
}
template<class Validate,class Pick,class Reject,class Persist>
std::string Select(std::string path,Validate valid,Pick pick,Reject reject,Persist persist) {
 const std::string saved=path;
 while(!valid(path)) {if(!path.empty())reject(path);path=pick();if(path.empty())return {};}
 if(path!=saved)persist(path);return path;
}
inline void EnsureWritable(const std::filesystem::path& dir) {
 std::filesystem::create_directories(dir);
 const auto probe=dir/(L"write-check-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 HANDLE h=CreateFileW(probe.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
 if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot write player configuration directory");CloseHandle(h);
}
inline std::filesystem::path UserDirectory() {
 wchar_t buffer[32768];DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,32768);
 if(!n || n>=32768)throw std::runtime_error("LOCALAPPDATA is unavailable");
 auto path=std::filesystem::path(buffer)/L"Burnout3-PAL-Enhanced";EnsureWritable(path);return path;
}
inline std::filesystem::path NewRun(const std::filesystem::path& root) {
 for(unsigned n=0;n<100;++n){auto p=root/L"logs"/(L"run-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(n));
 std::filesystem::create_directories(p.parent_path());if(std::filesystem::create_directory(p))return p;}
 throw std::runtime_error("Cannot create exclusive player log directory");
}
}
