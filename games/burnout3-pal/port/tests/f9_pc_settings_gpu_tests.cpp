// Short native-window/renderer probe. Uses synthetic pixels, never runs the game.
#include "ps2host/run.h"
#include "ps2host/settings.h"
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {
void Require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
std::string Read(const std::filesystem::path& p) {
    std::ifstream in(p); return std::string(std::istreambuf_iterator<char>(in), {});
}
}

int main(int argc, char** argv) {
    try {
        Require(argc == 3, "mode and isolated profile required");
        const bool restart = std::string(argv[1]) == "--restart";
        const auto folder = std::filesystem::u8path(argv[2]);
        const auto ini = folder / "pc_settings.ini";
        if (!restart) {
            Require(std::filesystem::create_directories(folder), "profile must be exclusive");
            std::ofstream(ini) << "gpu_thread=off\nvsync=on\naspect=4:3\ncustom=keep\n";
        }
        ps2host::LoadSettings(ini.u8string());
        Require(ps2host::Setting("gpu_thread") == (restart ? "on" : "off"), "saved startup preference");
        ps2host::RunSetting("vsync", "off");
        ps2host::RunSetting("fullscreen", "off");
        ps2host::RunSetting("resolution", "2x");
        ps2host::RunSetting("aspect", "16:9");
        Require(ps2host::OpenWindow(L"PAL F9-A settings probe", 640, 448), "native window");
        std::string error;
        Require(ps2host::StartGpuRenderer(&error, ps2host::GpuMode::Primary, false), error.c_str());
        const std::string active = restart ? "on" : "off";
        Require(ps2host::EffectiveSetting("gpu_thread") == active, "real renderer effective state");
        ps2host::GpuSetScale(2);
        auto rows = ps2host::PcSettings(); int gpu = -1, fps = -1;
        for (std::size_t n = 0; n < rows.size(); ++n) {
            if (rows[n].label == "GPU THREAD") gpu = int(n);
            if (rows[n].label == "FPS COUNTER") fps = int(n);
        }
        Require(gpu >= 0 && fps >= 0, "functional rows available");
        ps2host::OpenPcMenu(); Require(ps2host::PcMenuOpen(), "real overlay opens");
        ps2host::StepPcSetting(gpu, 1);
        Require(ps2host::EffectiveSetting("gpu_thread") == active, "preference does not hot-toggle renderer");
        Require(ps2host::PcSettings()[gpu].value.find("(restart)") != std::string::npos, "pending restart visible");
        const bool fps_before = ps2host::SettingOn("fps_counter", false);
        ps2host::StepPcSetting(fps, 1);
        Require(ps2host::SettingOn("fps_counter") != fps_before, "FPS preference toggled");
        ps2host::SetWindowVsync(true); Require(!ps2host::WindowVsync(), "CLI vsync wins in real display");
        ps2host::SetWindowFullscreen(true); Require(!ps2host::WindowFullscreen(), "CLI fullscreen wins in real display");
        ps2host::SetWindowAspect("4:3"); Require(ps2host::Setting("aspect") == "16:9", "CLI aspect retained");
        ps2host::SetPictureSource([](std::vector<u32>* p, u32* w, u32* h) {
            *w = 320; *h = 224; p->assign(320 * 224, 0xFF204080); return true;
        });
        ps2host::PresentFrame(); Require(!ps2host::WindowClosed(), "synthetic window presentation");
        Require(Read(ini).find("vsync=on") != std::string::npos && Read(ini).find("custom=keep") != std::string::npos,
                "runtime overrides do not leak during F9 save");
        Require(Read(ini).find(restart ? "gpu_thread=off" : "gpu_thread=on") != std::string::npos,
                "new preference saved for next startup");
        std::printf("Real F9/GPU startup: active %s, pending %s; display CLI locks, save and synthetic present PASS\n",
                    active.c_str(), restart ? "off" : "on");
        // Established PAL native-window lifetime policy; no game or GS queue
        // was started. The supervisor checks process/Job cleanup independently.
        ps2host::ReleaseController(); std::fflush(nullptr);
        TerminateProcess(GetCurrentProcess(), 0); return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "F9 GPU probe failed: %s\n", e.what()); std::fflush(nullptr);
        TerminateProcess(GetCurrentProcess(), 1); return 1;
    }
}
