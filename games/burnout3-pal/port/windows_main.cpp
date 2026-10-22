// PAL entry point adapted from engine/host/src/main.cpp at d7e6e023. MIT license.
/*
 * Every game port's entry point. The game supplies its translated functions (the
 * table engine/tools/port.py generates) and its overrides; the rest is shared.
 */

#include <cctype>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <io.h>
#include <sys/stat.h>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#include "ps2host/disc.h"
#include "player_startup.h"
#undef PS2HOST_GAME_VERSION
#define PS2HOST_GAME_VERSION "0.1.0-alpha"
#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

extern const ps2host::FunctionEntry kGuestFunctions[];
extern const std::size_t kGuestFunctionCount;
extern const ps2host::OverlayEntry kGuestOverlays[];
extern const std::size_t kGuestOverlayCount;

/* The game's host implementations of guest functions (its port's overrides.cpp). */
void RegisterGameOverrides();

namespace {

/* The port is built for AVX2 (engine/host/CMakeLists.txt; the VU's lanes and
   the rest take its three-operand forms): a processor without it is told so
   rather than stopped by an illegal instruction somewhere. */
bool HasAvx2() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86)) && defined(__AVX2__)
    int info[4];
    __cpuid(info, 0);
    if (info[0] < 7) return false;
    __cpuid(info, 1);
    const bool osxsave = (info[2] >> 27) & 1, avx = (info[2] >> 28) & 1;
    if (!osxsave || !avx) return false;
    if ((_xgetbv(0) & 6) != 6) return false;        // the OS saves the YMM registers
    __cpuidex(info, 7, 0);
    return (info[1] >> 5) & 1;
#else
    return true;
#endif
}

std::string g_log;     // the log a run started without arguments writes

#ifdef PS2HOST_GAME_EXECUTABLE
/* Whether `path` is an image of the game's disc: the executable it boots is
   the one the port was translated from (game.json's). */
bool IsGameDisc(const std::string& path) { return pal_player::CompatibleDisc(path); }
std::string PickDisc() {
 wchar_t file[32768]={};OPENFILENAMEW dialog={};dialog.lStructSize=sizeof dialog;
 dialog.lpstrFilter=L"PlayStation 2 disc image (*.iso)\0*.iso\0";dialog.lpstrFile=file;dialog.nMaxFile=32768;
 dialog.lpstrTitle=L"Choose your Burnout 3 PAL disc (SLES_525.84)";
 dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_HIDEREADONLY|OFN_NOCHANGEDIR;
 if(GetOpenFileNameW(&dialog))return pal_player::Utf8(file);
 if(CommDlgExtendedError())throw std::runtime_error("Windows could not open the disc selector");return {};
}

#endif

/* Started without arguments - from Explorer, a shortcut - the port plays its
   game from the player's own disc image: the one chosen before (kept in the
   PC settings, beside the executable), or asked for and checked to be the
   game's disc; the memory card a directory beside it too. In a window, with
   sound, on the GPU, with no time limit (the default is for test runs).
   An empty list: the player gave none. */
std::vector<std::string> PlayArguments(const char* self) {
 const auto here=pal_player::UserDirectory();
 const auto settings=here/L"pc_settings.ini";
 ps2host::LoadSettings(settings.u8string());
 std::string disc=pal_player::Select(ps2host::Setting("disc"),IsGameDisc,PickDisc,
 [](const std::string&){MessageBoxW(nullptr,L"The selected disc is missing, invalid or incompatible. Choose Burnout 3 PAL (SLES_525.84), not NTSC-U.",L"Burnout 3 PAL Enhanced",MB_OK|MB_ICONWARNING);},
 [&](const std::string& path){ps2host::SetSetting("disc",path);
 // Settings writes report errors to stderr; confirm persistence before playing.
 std::ifstream in(settings);std::string line;bool saved=false;while(std::getline(in,line))if(line=="disc="+path)saved=true;
 if(!saved)throw std::runtime_error("Cannot save the selected disc path");});
 if(disc.empty())return {};
 for(const auto& pair:std::vector<std::pair<std::string,std::string>>{{"resolution","2x"},{"aspect","16:9"},{"frame_pacing","on"},{"vsync","off"},{"antialiasing","off"},{"fullscreen","off"}})
 if(ps2host::Setting(pair.first).empty())ps2host::SetSetting(pair.first,pair.second);
 char* audit=nullptr;size_t size=0;_dupenv_s(&audit,&size,"PAL_RELOCATION_AUDIT_DIR");
 std::filesystem::path run;
 if(audit && *audit)run=std::filesystem::u8path(audit);else run=pal_player::NewRun(here);free(audit);
 pal_player::EnsureWritable(run);_putenv_s("PAL_RELOCATION_AUDIT_DIR",run.u8string().c_str());
 g_log=(run/L"player.log").u8string();
 std::FILE* stream=nullptr;freopen_s(&stream,"NUL","w",stdout);freopen_s(&stream,"NUL","w",stderr);
 const int file=_wopen((run/L"player.log").c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_TEXT,_S_IREAD|_S_IWRITE);
 if(file<0)throw std::runtime_error("Cannot create player log");_dup2(file,_fileno(stdout));_dup2(file,_fileno(stderr));_close(file);
 std::setvbuf(stdout,nullptr,_IONBF,0);std::setvbuf(stderr,nullptr,_IONBF,0);
 return {self,"--disc",disc,"--memcard",(here/L"memcard").u8string(),"--window","--gpu","--audio","--timeout","315360000"};
}

}  // namespace

/* A port built as a windowed program has no console of its own. Started from
   a terminal its output goes to that one; redirected (a file, a pipe: the
   tools' runs), where it was sent. */
void AttachTerminal() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    std::FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
}

int main(int argc, char** argv) {
    AttachTerminal();
    try {
    if(GetACP()!=CP_UTF8)throw std::runtime_error("This player requires Windows 10 version 1903 or newer (UTF-8 process manifest)");
    // CLI launches also need the existing PAL audit handler's private directory.
    if(argc>1 && !std::getenv("PAL_RELOCATION_AUDIT_DIR")) {
      auto run=pal_player::NewRun(pal_player::UserDirectory());_putenv_s("PAL_RELOCATION_AUDIT_DIR",run.u8string().c_str());
    }
    std::vector<std::string> play;
    std::vector<char*> play_argv;
    if (argc == 1) {
        play = PlayArguments(argv[0]);
        if (play.empty()) return 0;
        for (std::string& arg : play) play_argv.push_back(arg.data());
        play_argv.push_back(nullptr);
        argc = int(play.size());
        argv = play_argv.data();
    }
    if (!HasAvx2()) {
        std::fprintf(stderr, "This build needs a processor with AVX2 (Intel Haswell, AMD Excavator or later).\n");
        return 70;
    }
#ifdef PS2HOST_GAME_TITLE
    ps2host::SetGameTitle(PS2HOST_GAME_TITLE);      // the window's (game.json's "title")
#endif
#ifdef PS2HOST_GAME_VERSION
    std::fprintf(stderr, "[port] %s, version %s\n", PS2HOST_GAME_TITLE, PS2HOST_GAME_VERSION);
#endif
    ps2host::RegisterFunctions(kGuestFunctions, kGuestFunctionCount);
    ps2host::RegisterOverlays(kGuestOverlays, kGuestOverlayCount);
    RegisterGameOverrides();
    ps2host::EnablePalRegionalPresentation();
    const int code = ps2host::Main(argc, argv);
    if (!play.empty() && code != 0) {         // closing the window ends a run with 0
        std::fflush(nullptr);
        const std::string text = "The game stopped because of an error. What happened is in " +
                                 g_log;
        MessageBoxA(nullptr, text.c_str(), ps2host::GameTitle().empty() ? "PS2 port" : ps2host::GameTitle().c_str(),
                    MB_OK | MB_ICONERROR);
    }
    // What must be finished is (the report written, the settings and the
    // memory card saved as they changed, the WAV's header here); the rest -
    // the window, the GPU device, the threads - goes with the process.
    // Taken down one by one at exit, the Vulkan device's could hang
    // (vkDestroyDevice with the driver's threads and the renderer's images
    // still about), leaving a window that never closed.
    ps2host::CloseAudio();
    ps2host::ReleaseController();
    std::fflush(nullptr);
    TerminateProcess(GetCurrentProcess(), UINT(code));
    return code;
    } catch(const std::exception& error) {
      MessageBoxW(nullptr,pal_player::Wide(error.what()).c_str(),L"Burnout 3 PAL Enhanced - startup error",MB_OK|MB_ICONERROR);return 1;
    }
}
