/*
 * The port's window (--window): the GS's displayed picture, every vertical
 * blank, scaled to the window, and the keyboard the controller reads.
 *
 * With the GPU display built in (PS2HOST_VULKAN, engine/gpu) the window is an
 * SDL3 window presenting through Vulkan; without it, or when no Vulkan device
 * can be had, a plain Win32 window drawn with GDI. Both are pumped on the host's
 * thread at each frame (FrameDone).
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <memory>
#include <string>
#include <functional>
#include <vector>

#include "ps2host/gs.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

#ifdef PS2HOST_VULKAN
#include <SDL3/SDL_scancode.h>

#include "ps2gpu/display.h"
#endif

namespace ps2host {

namespace {

HWND g_window = nullptr;
bool g_closed = false;

#ifdef PS2HOST_VULKAN
std::unique_ptr<ps2gpu::Display> g_display;

// The keys the controller reads, as Win32 virtual keys, by position.
int Scancode(int key) {
    switch (key) {
        case VK_BACK: return SDL_SCANCODE_BACKSPACE;
        case VK_RETURN: return SDL_SCANCODE_RETURN;
        case VK_UP: return SDL_SCANCODE_UP;
        case VK_DOWN: return SDL_SCANCODE_DOWN;
        case VK_LEFT: return SDL_SCANCODE_LEFT;
        case VK_RIGHT: return SDL_SCANCODE_RIGHT;
        default: break;
    }
    if (key >= 'A' && key <= 'Z') return SDL_SCANCODE_A + (key - 'A');
    if (key >= '1' && key <= '9') return SDL_SCANCODE_1 + (key - '1');
    if (key == '0') return SDL_SCANCODE_0;
    return -1;
}
#endif

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
        case WM_CLOSE:
            g_closed = true;
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            g_window = nullptr;
            return 0;
        default:
            return DefWindowProcW(window, message, w, l);
    }
}

bool OpenGdiWindow(const wchar_t* title, int width, int height) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ps2host";
    RegisterClassW(&wc);
    RECT r = {0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_window = CreateWindowW(L"ps2host", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                             CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                             wc.hInstance, nullptr);
    return g_window != nullptr;
}

void PresentGdi() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!g_window) return;
    std::vector<u32> pixels;
    u32 w = 0, h = 0;
    if (!gs::TheGs().Picture(&pixels, &w, &h)) return;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = LONG(w);
    info.bmiHeader.biHeight = -LONG(h);                 // top-down rows
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    RECT client;
    GetClientRect(g_window, &client);
    HDC dc = GetDC(g_window);
    SetStretchBltMode(dc, HALFTONE);
    StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0, LONG(w), LONG(h), pixels.data(), &info,
                  DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_window, dc);
}

}  // namespace

bool OpenWindow(const wchar_t* title, int width, int height) {
#ifdef PS2HOST_VULKAN
    std::string narrow;
    for (const wchar_t* p = title; *p; ++p) narrow += char(*p < 128 ? *p : '?');
    std::string error;
    g_display = ps2gpu::CreateVulkanDisplay(narrow.c_str(), width, height, &error);
    if (g_display) {
        Note("window", "window: Vulkan on " + g_display->Describe());
        if (SettingOn("fullscreen")) g_display->SetFullscreen(true);
        if (SettingOn("vsync")) g_display->SetVsync(true);
        return true;
    }
    Note("window", "window: no Vulkan display (" + error + "); drawing with GDI");
#endif
    return OpenGdiWindow(title, width, height);
}

bool WindowOpen() {
#ifdef PS2HOST_VULKAN
    if (g_display) return !g_closed;
#endif
    return g_window != nullptr;
}

bool WindowFocused() {
#ifdef PS2HOST_VULKAN
    if (g_display) return g_display->Focused();
#endif
    return g_window && GetForegroundWindow() == g_window;
}

bool WindowClosed() { return g_closed; }

bool HostKeyDown(int key) {
#ifdef PS2HOST_VULKAN
    if (g_display) return g_display->KeyDown(Scancode(key));
#endif
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool HostPad(HostPadState* state) {
#ifdef PS2HOST_VULKAN
    ps2gpu::Display::PadState pad;
    if (g_display && g_display->Pad(&pad)) {
        state->buttons = pad.buttons;
        for (int i = 0; i < 4; ++i) state->sticks[i] = pad.sticks[i];
        state->l2 = pad.l2;
        state->r2 = pad.r2;
        return true;
    }
#endif
    (void)state;
    return false;   // the GDI window has none
}

namespace {
// --gpu-show: where the window's picture comes from instead of the software GS.
std::function<bool(std::vector<u32>*, u32*, u32*)> g_picture_source;
}  // namespace

#ifdef PS2HOST_VULKAN
ps2gpu::Gpu* WindowGpu() { return g_display ? g_display->SharedGpu() : nullptr; }
#endif

void SetPictureSource(std::function<bool(std::vector<u32>*, u32*, u32*)> source) {
    g_picture_source = std::move(source);
}

// Alt+Enter changes fullscreen in the display's Pump: saved as the setting.
void KeepFullscreenSetting() {
#ifdef PS2HOST_VULKAN
    static int last = -1;
    const int now = g_display && g_display->Fullscreen() ? 1 : 0;
    if (last >= 0 && now != last) SetSettingOn("fullscreen", now != 0);
    last = now;
#endif
}

bool PumpWindow() {
#ifdef PS2HOST_VULKAN
    if (g_display) {
        if (!g_display->Pump()) g_closed = true;
        KeepFullscreenSetting();
        if (g_display->TakeCapture()) RequestCapture();
        return !g_closed;
    }
#endif
    if (g_window) PresentGdi();
    return true;
}

void SetWindowFullscreen(bool on) {
    SetSettingOn("fullscreen", on);
#ifdef PS2HOST_VULKAN
    if (g_display) g_display->SetFullscreen(on);
#endif
}

void SetWindowVsync(bool on) {
    SetSettingOn("vsync", on);
#ifdef PS2HOST_VULKAN
    if (g_display) g_display->SetVsync(on);
#endif
}

bool WindowVsync() {
#ifdef PS2HOST_VULKAN
    if (g_display) return g_display->Vsync();
#endif
    return SettingOn("vsync");
}

bool WindowFullscreen() {
#ifdef PS2HOST_VULKAN
    if (g_display) return g_display->Fullscreen();
#endif
    return SettingOn("fullscreen");
}

ps2gpu::Display* WindowDisplay() {
#ifdef PS2HOST_VULKAN
    return g_display.get();
#else
    return nullptr;
#endif
}

void PresentFrame() {
#ifdef PS2HOST_VULKAN
    if (g_display) {
        if (!g_display->Pump()) {
            g_closed = true;
            return;
        }
        KeepFullscreenSetting();
        std::vector<u32> pixels;
        u32 w = 0, h = 0;
        const bool got = (g_picture_source && g_picture_source(&pixels, &w, &h)) || gs::TheGs().Picture(&pixels, &w, &h);
        if (got && !g_display->Present(pixels.data(), w, h)) g_closed = true;
        return;
    }
#endif
    if (g_window) PresentGdi();
}

}  // namespace ps2host
