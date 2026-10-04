/*
 * The port's display: a window showing the console's picture, and the keyboard
 * and window events that come with it.
 *
 * Display is what the host talks to; VulkanDisplay (SDL3 + Vulkan) is the one
 * implementation so far. Stage one presents a finished picture - the software
 * GS's - scaled to the window; a GPU GS renderer will draw into the same
 * swapchain later.
 */

#ifndef PS2GPU_DISPLAY_H
#define PS2GPU_DISPLAY_H

#include <cstdint>
#include <memory>
#include <string>

namespace ps2gpu {

class Display {
public:
    virtual ~Display() = default;

    /* Shows a picture: 32-bit pixels, BGRA in memory (B first), rows top-down,
       scaled to the window. False when the window is gone. */
    virtual bool Present(const std::uint32_t* pixels, std::uint32_t width, std::uint32_t height) = 0;

    /* Handles the window's events; false once it has been closed. */
    virtual bool Pump() = 0;

    /* Whether the window has the keyboard, and whether a key is down - by SDL
       scancode (SDL_SCANCODE_*), which names keys by position. */
    virtual bool Focused() const = 0;
    virtual bool KeyDown(int scancode) const = 0;

    /* The GPU and driver, for the report. */
    virtual std::string Describe() const = 0;

    /* The window's device as a Gpu, for a GsRenderer to draw with: one
       Vulkan instance and device for both (owned by the display). */
    virtual class Gpu* SharedGpu() = 0;
};

/* A window with a Vulkan swapchain; null, with `error` saying why, when there is
   no Vulkan device or window to be had. */
std::unique_ptr<Display> CreateVulkanDisplay(const char* title, int width, int height, std::string* error);

}  // namespace ps2gpu

#endif  /* PS2GPU_DISPLAY_H */
