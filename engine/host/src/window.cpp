/*
 * The port's window (--window): the GS's displayed picture, every vertical
 * blank, scaled to the window. Win32 and GDI only, so the host needs nothing
 * beyond the system; a GPU renderer can replace the presentation later without
 * changing who calls it.
 *
 * Everything runs on the host's one thread: the window is pumped at each frame
 * (FrameDone), which is also where input will be read.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <vector>

#include "ps2host/gs.h"
#include "ps2host/run.h"

namespace ps2host {

namespace {

HWND g_window = nullptr;
bool g_closed = false;

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

}  // namespace

bool OpenWindow(const wchar_t* title, int width, int height) {
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

bool WindowOpen() { return g_window != nullptr; }
bool WindowClosed() { return g_closed; }

void PresentFrame() {
    if (!g_window) return;
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

}  // namespace ps2host
