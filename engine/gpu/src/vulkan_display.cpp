/*
 * VulkanDisplay: an SDL3 window and a Vulkan swapchain presenting a picture.
 *
 * Each frame the picture goes into a host-visible staging buffer, is copied to
 * an image, and blitted - scaled, 4:3, letterboxed - onto the swapchain image,
 * which is cleared to black first. One frame is in flight: the fence is waited
 * before the staging buffer is written again. The swapchain presents in
 * MAILBOX mode where the driver has it (never blocking the emulation thread),
 * else IMMEDIATE, else FIFO, and is recreated when the window changes size.
 */

#include "ps2gpu/display.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <volk.h>

#include "gpu_internal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace ps2gpu {

namespace {

class VulkanDisplay final : public Display {
public:
    ~VulkanDisplay() override;
    bool Init(const char* title, int width, int height);
    bool Present(const std::uint32_t* pixels, std::uint32_t width, std::uint32_t height) override;
    bool PresentImage(std::uint64_t image, int layout, std::uint32_t width, std::uint32_t height) override;
    bool PresentImageAt(std::uint64_t image, int layout, std::uint32_t width, std::uint32_t height,
                        std::uint64_t due_ns) override;
    bool Pump() override;
    void SetFullscreen(bool on) override;
    bool Fullscreen() const override;
    void SetVsync(bool on) override;
    bool Vsync() const override { return vsync_; }
    Pacing TakePacing() override;
    bool Focused() const override;
    bool KeyDown(int scancode) const override;
    bool Pad(PadState* state) const override;
    std::string Describe() const override { return describe_; }
    Gpu* SharedGpu() override;
    const std::string& error() const { return error_; }

private:
    bool Check(VkResult result, const char* what);
    bool CreateSwapchain();
    void DestroySwapchain();
    bool EnsurePicture(std::uint32_t width, std::uint32_t height);
    void DestroyPicture();
    std::uint32_t FindMemory(std::uint32_t bits, VkMemoryPropertyFlags wanted) const;
    // PresentImageAt's: pictures copied and waiting, and the thread that
    // shows them when they are due.
    bool EnsurePaced(std::uint32_t width, std::uint32_t height);
    void DestroyPaced();
    void PresentLoop();

    SDL_Window* window_ = nullptr;
    SDL_Gamepad* gamepad_ = nullptr;        // the first controller plugged in
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t family_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkSemaphore> rendered_;     // one per swapchain image
    VkCommandPool pool_ = VK_NULL_HANDLE;
    // Presents in flight: each its command buffer, acquire semaphore and
    // fence, so a present waits for the one three before it, not the last
    // (with one, every present waited for all the GPU had been given - the
    // GS thread, which presents, in step with the GPU once a vertical blank).
    static constexpr int kInFlight = 3;
    VkCommandBuffer cmds_[kInFlight] = {};
    VkSemaphore acquireds_[kInFlight] = {};
    VkFence fences_[kInFlight] = {};
    int slot_ = 0;
    // The current present's (slot_'s).
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkSemaphore acquired_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkPresentModeKHR mode_ = VK_PRESENT_MODE_FIFO_KHR;
    std::atomic<bool> vsync_{false};        // FIFO when set (SetVsync)

    VkBuffer staging_ = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
    VkImage picture_ = VK_NULL_HANDLE;
    VkDeviceMemory picture_memory_ = VK_NULL_HANDLE;
    std::uint32_t picture_width_ = 0, picture_height_ = 0;

    // Set by Pump (the main thread), read where pictures are presented (the
    // GS thread, when it presents).
    std::atomic<bool> closed_{false}, stale_{false};
    /* Acquires the next swapchain image and begins the command buffer:
       false (with *ok) when there is nothing to present into. */
    bool BeginPresent(std::uint32_t* index, bool* ok);
    /* Clears the swapchain image, blits `source` (TRANSFER_SRC) to it 4:3,
       submits and presents. */
    bool FinishPresent(std::uint32_t index, VkImage source, std::uint32_t width, std::uint32_t height);
    /* Ends the command buffer, submits it and presents the image. */
    bool Submit(std::uint32_t index);
    bool dual_source_ = false;
    std::string describe_, error_;
    // PresentImageAt's pictures: copies of the frames' targets, three, each
    // its copy's command buffer and fence (the GS thread's own pool).
    static constexpr int kPaced = 3;
    struct PacedPicture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkCommandBuffer copy = VK_NULL_HANDLE;
        VkFence copied = VK_NULL_HANDLE;
        std::uint32_t width = 0, height = 0;
        std::uint64_t due_ns = 0;
    };
    PacedPicture paced_[kPaced];
    std::uint32_t paced_width_ = 0, paced_height_ = 0;
    VkCommandPool copy_pool_ = VK_NULL_HANDLE;
    std::mutex paced_lock_;                 // ready_, taken_, stop_
    std::condition_variable paced_wake_;
    std::deque<int> ready_;                 // pictures waiting to be shown, oldest first
    int taken_ = -1;                        // the one being shown
    bool stop_ = false;
    std::thread present_thread_;
    std::mutex present_lock_;               // the swapchain and cmds_: the present thread, or Present
    // Present times (TakePacing): intervals since the last take.
    std::mutex pacing_lock_;
    std::chrono::steady_clock::time_point last_present_{};
    std::uint64_t presents_ = 0;
    double interval_sum_ = 0, interval_squares_ = 0, interval_longest_ = 0;
    std::unique_ptr<VulkanGpu> shared_;
};

Gpu* VulkanDisplay::SharedGpu() {
    if (!shared_) {
        auto gpu = std::make_unique<VulkanGpu>();
        std::string error;
        if (!gpu->InitShared(instance_, gpu_, device_, family_, queue_, dual_source_, describe_, &error))
            return nullptr;
        shared_ = std::move(gpu);
    }
    return shared_.get();
}

bool VulkanDisplay::Check(VkResult result, const char* what) {
    if (result == VK_SUCCESS) return true;
    char text[160];
    std::snprintf(text, sizeof text, "%s failed (VkResult %d)", what, int(result));
    error_ = text;
    return false;
}

bool VulkanDisplay::Init(const char* title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        error_ = std::string("SDL_Init: ") + SDL_GetError();
        return false;
    }
    // Controllers: optional (the keyboard still plays), and read whichever
    // window has focus - the console's pad does not care either.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "[window] no game controllers: %s\n", SDL_GetError());
    } else {
        int joysticks = 0, gamepads = 0;
        // Wireless controllers can take a moment to be found.
        for (int waited = 0; waited < 1000 && joysticks == 0; waited += 50) {
            SDL_PumpEvents();
            SDL_free(SDL_GetJoysticks(&joysticks));
            if (joysticks == 0) SDL_Delay(50);
        }
        SDL_free(SDL_GetGamepads(&gamepads));
        std::fprintf(stderr, "[window] %d controller(s) plugged in, %d of them with a known layout\n", joysticks,
                     gamepads);
    }
    if (!Check(volkInitialize(), "volkInitialize (is there a Vulkan driver?)")) return false;
    window_ = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        error_ = std::string("SDL_CreateWindow: ") + SDL_GetError();
        return false;
    }

    // The instance, with the extensions SDL needs for its surface.
    Uint32 count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = title;
    app.apiVersion = VK_API_VERSION_1_3;      // the GS renderer draws on this device too
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = count;
    ici.ppEnabledExtensionNames = extensions;
    if (!Check(vkCreateInstance(&ici, nullptr, &instance_), "vkCreateInstance")) return false;
    volkLoadInstance(instance_);
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        error_ = std::string("SDL_Vulkan_CreateSurface: ") + SDL_GetError();
        return false;
    }

    // A GPU with a queue that draws and presents; a discrete one first.
    std::uint32_t gpus = 0;
    vkEnumeratePhysicalDevices(instance_, &gpus, nullptr);
    std::vector<VkPhysicalDevice> list(gpus);
    vkEnumeratePhysicalDevices(instance_, &gpus, list.data());
    int best = -1;
    for (VkPhysicalDevice candidate : list) {
        std::uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, props.data());
        for (std::uint32_t f = 0; f < families; ++f) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, f, surface_, &present);
            if (!(props[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(candidate, &p);
            const int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score > best) {
                best = score;
                gpu_ = candidate;
                family_ = f;
            }
            break;
        }
    }
    if (!gpu_) {
        error_ = "no Vulkan device can draw and present to the window";
        return false;
    }
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(gpu_, &p);
    char text[320];
    std::snprintf(text, sizeof text, "%s (Vulkan %u.%u.%u, driver %u)", p.deviceName,
                  VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion),
                  VK_API_VERSION_PATCH(p.apiVersion), p.driverVersion);
    describe_ = text;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // What the GS renderer needs, for when it draws on this device
    // (SharedGpu): dynamic rendering, and dual-source blending if there is any.
    VkPhysicalDeviceFeatures supported;
    vkGetPhysicalDeviceFeatures(gpu_, &supported);
    dual_source_ = supported.dualSrcBlend == VK_TRUE;
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.dynamicRendering = VK_TRUE;
    v13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.features.dualSrcBlend = supported.dualSrcBlend;
    features.features.logicOp = supported.logicOp;
    features.pNext = &v13;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &features;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = device_extensions;
    if (!Check(vkCreateDevice(gpu_, &dci, nullptr, &device_), "vkCreateDevice")) return false;
    // Device functions stay the loader's: see gpu.cpp.
    vkGetDeviceQueue(device_, family_, 0, &queue_);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = family_;
    if (!Check(vkCreateCommandPool(device_, &pci, nullptr, &pool_), "vkCreateCommandPool")) return false;
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = kInFlight;
    if (!Check(vkAllocateCommandBuffers(device_, &cai, cmds_), "vkAllocateCommandBuffers")) return false;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < kInFlight; ++i) {
        if (!Check(vkCreateSemaphore(device_, &sci, nullptr, &acquireds_[i]), "vkCreateSemaphore")) return false;
        if (!Check(vkCreateFence(device_, &fci, nullptr, &fences_[i]), "vkCreateFence")) return false;
    }
    cmd_ = cmds_[0];
    acquired_ = acquireds_[0];
    fence_ = fences_[0];
    return CreateSwapchain();
}

bool VulkanDisplay::CreateSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps);
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    extent_ = caps.currentExtent.width != 0xFFFFFFFFu ? caps.currentExtent
              : VkExtent2D{std::uint32_t(w), std::uint32_t(h)};
    if (extent_.width == 0 || extent_.height == 0) return true;      // minimised: nothing to show
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        error_ = "the swapchain cannot be a transfer destination";
        return false;
    }

    std::uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &n, formats.data());
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM) chosen = f;
    format_ = chosen.format;

    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &n, modes.data());
    mode_ = VK_PRESENT_MODE_FIFO_KHR;            // every device has it
    if (!vsync_)
        for (VkPresentModeKHR m : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR})
            if (std::find(modes.begin(), modes.end(), m) != modes.end()) mode_ = m;

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface_;
    sci.minImageCount = std::max(caps.minImageCount, 2u);
    if (caps.maxImageCount) sci.minImageCount = std::min(sci.minImageCount + 1, caps.maxImageCount);
    sci.imageFormat = format_;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = extent_;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = mode_;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = swapchain_;
    VkSwapchainKHR created = VK_NULL_HANDLE;
    if (!Check(vkCreateSwapchainKHR(device_, &sci, nullptr, &created), "vkCreateSwapchainKHR")) return false;
    DestroySwapchain();
    swapchain_ = created;
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
    images_.resize(n);
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, images_.data());
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    rendered_.resize(n);
    for (auto& s : rendered_)
        if (!Check(vkCreateSemaphore(device_, &semaphore, nullptr, &s), "vkCreateSemaphore")) return false;
    stale_ = false;
    return true;
}

void VulkanDisplay::DestroySwapchain() {
    if (!device_) return;
    {
        std::lock_guard<std::mutex> hold(g_queue_lock);
        vkDeviceWaitIdle(device_);
    }
    for (VkSemaphore s : rendered_) vkDestroySemaphore(device_, s, nullptr);
    rendered_.clear();
    images_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

std::uint32_t VulkanDisplay::FindMemory(std::uint32_t bits, VkMemoryPropertyFlags wanted) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(gpu_, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
    return ~0u;
}

void VulkanDisplay::DestroyPicture() {
    if (!device_) return;
    if (mapped_) vkUnmapMemory(device_, staging_memory_);
    mapped_ = nullptr;
    if (staging_) vkDestroyBuffer(device_, staging_, nullptr);
    if (staging_memory_) vkFreeMemory(device_, staging_memory_, nullptr);
    if (picture_) vkDestroyImage(device_, picture_, nullptr);
    if (picture_memory_) vkFreeMemory(device_, picture_memory_, nullptr);
    staging_ = VK_NULL_HANDLE;
    staging_memory_ = picture_memory_ = VK_NULL_HANDLE;
    picture_ = VK_NULL_HANDLE;
    picture_width_ = picture_height_ = 0;
}

bool VulkanDisplay::EnsurePicture(std::uint32_t width, std::uint32_t height) {
    if (width == picture_width_ && height == picture_height_ && picture_) return true;
    {
        std::lock_guard<std::mutex> hold(g_queue_lock);
        vkDeviceWaitIdle(device_);
    }
    DestroyPicture();
    const VkDeviceSize bytes = VkDeviceSize(width) * height * 4;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (!Check(vkCreateBuffer(device_, &bci, nullptr, &staging_), "vkCreateBuffer")) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, staging_, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!Check(vkAllocateMemory(device_, &mai, nullptr, &staging_memory_), "vkAllocateMemory (staging)"))
        return false;
    vkBindBufferMemory(device_, staging_, staging_memory_, 0);
    vkMapMemory(device_, staging_memory_, 0, bytes, 0, &mapped_);

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_B8G8R8A8_UNORM;
    ici.extent = {width, height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!Check(vkCreateImage(device_, &ici, nullptr, &picture_), "vkCreateImage")) return false;
    vkGetImageMemoryRequirements(device_, picture_, &req);
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!Check(vkAllocateMemory(device_, &mai, nullptr, &picture_memory_), "vkAllocateMemory (picture)"))
        return false;
    vkBindImageMemory(device_, picture_, picture_memory_, 0);
    picture_width_ = width;
    picture_height_ = height;
    return true;
}

void Barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
             VkAccessFlags src, VkAccessFlags dst, VkPipelineStageFlags src_stage,
             VkPipelineStageFlags dst_stage) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from;
    b.newLayout = to;
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

bool VulkanDisplay::BeginPresent(std::uint32_t* index, bool* ok) {
    *ok = true;
    if (closed_) {
        *ok = false;
        return false;
    }
    if (stale_ && !CreateSwapchain()) {
        *ok = false;
        return false;
    }
    if (!swapchain_ || extent_.width == 0 || extent_.height == 0) return false;
    slot_ = (slot_ + 1) % kInFlight;
    cmd_ = cmds_[slot_];
    acquired_ = acquireds_[slot_];
    fence_ = fences_[slot_];
    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
    VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, acquired_, VK_NULL_HANDLE, index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        stale_ = true;
        return false;
    }
    if (r != VK_SUBOPTIMAL_KHR && !Check(r, "vkAcquireNextImageKHR")) {
        *ok = false;
        return false;
    }
    vkResetFences(device_, 1, &fence_);
    vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_, &begin);
    return true;
}

bool VulkanDisplay::FinishPresent(std::uint32_t index, VkImage source, std::uint32_t width, std::uint32_t height) {
    const VkImage target = images_[index];
    Barrier(cmd_, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    const VkClearColorValue black{};
    const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd_, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &all);
    // 4:3, as a television shows it, centred in the window.
    std::int32_t w = std::int32_t(extent_.width), h = std::int32_t(extent_.height);
    std::int32_t dw = w, dh = h;
    if (std::int64_t(w) * 3 > std::int64_t(h) * 4) dw = h * 4 / 3;
    else dh = w * 3 / 4;
    const std::int32_t x0 = (w - dw) / 2, y0 = (h - dh) / 2;
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {std::int32_t(width), std::int32_t(height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {x0, y0, 0};
    blit.dstOffsets[1] = {x0 + dw, y0 + dh, 1};
    vkCmdBlitImage(cmd_, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    Barrier(cmd_, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    return true;
}

bool VulkanDisplay::Present(const std::uint32_t* pixels, std::uint32_t width, std::uint32_t height) {
    if (!width || !height) return !closed_;
    std::lock_guard<std::mutex> hold(present_lock_);
    std::uint32_t index = 0;
    bool ok = true;
    if (!BeginPresent(&index, &ok)) return ok;
    if (!EnsurePicture(width, height)) {
        vkEndCommandBuffer(cmd_);
        return false;
    }
    // One staging buffer and picture for all: what an earlier present still
    // copies from goes first.
    for (int i = 0; i < kInFlight; ++i)
        if (i != slot_) vkWaitForFences(device_, 1, &fences_[i], VK_TRUE, UINT64_MAX);
    std::memcpy(mapped_, pixels, std::size_t(width) * height * 4);
    Barrier(cmd_, picture_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd_, staging_, picture_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    Barrier(cmd_, picture_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    FinishPresent(index, picture_, width, height);
    return Submit(index);
}

bool VulkanDisplay::PresentImage(std::uint64_t image, int layout, std::uint32_t width, std::uint32_t height) {
    if (!width || !height || !image) return !closed_;
    std::lock_guard<std::mutex> hold(present_lock_);
    std::uint32_t index = 0;
    bool ok = true;
    if (!BeginPresent(&index, &ok)) return ok;
    const VkImage source = reinterpret_cast<VkImage>(image);
    const VkImageLayout kept = VkImageLayout(layout);
    // Whatever drew it was submitted before on this queue: a barrier from
    // all of it, and back to the layout its renderer has it in.
    Barrier(cmd_, source, kept, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    FinishPresent(index, source, width, height);
    Barrier(cmd_, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kept, VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    return Submit(index);
}

bool VulkanDisplay::EnsurePaced(std::uint32_t width, std::uint32_t height) {
    if (width == paced_width_ && height == paced_height_ && paced_[0].image) return true;
    {
        std::unique_lock<std::mutex> hold(paced_lock_);
        ready_.clear();
        paced_wake_.wait(hold, [&] { return taken_ < 0; });   // none being shown
    }
    {
        std::lock_guard<std::mutex> hold(g_queue_lock);
        vkDeviceWaitIdle(device_);
    }
    DestroyPaced();
    if (!copy_pool_) {
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = family_;
        if (!Check(vkCreateCommandPool(device_, &pci, nullptr, &copy_pool_), "vkCreateCommandPool")) return false;
    }
    for (PacedPicture& p : paced_) {
        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_B8G8R8A8_UNORM;
        ici.extent = {width, height, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (!Check(vkCreateImage(device_, &ici, nullptr, &p.image), "vkCreateImage (paced)")) return false;
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device_, p.image, &req);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (!Check(vkAllocateMemory(device_, &mai, nullptr, &p.memory), "vkAllocateMemory (paced)")) return false;
        vkBindImageMemory(device_, p.image, p.memory, 0);
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = copy_pool_;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        if (!Check(vkAllocateCommandBuffers(device_, &cai, &p.copy), "vkAllocateCommandBuffers (paced)")) return false;
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (!Check(vkCreateFence(device_, &fci, nullptr, &p.copied), "vkCreateFence (paced)")) return false;
    }
    paced_width_ = width;
    paced_height_ = height;
    if (!present_thread_.joinable()) present_thread_ = std::thread([this] { PresentLoop(); });
    return true;
}

void VulkanDisplay::DestroyPaced() {
    if (!device_) return;
    for (PacedPicture& p : paced_) {
        if (p.copied) vkDestroyFence(device_, p.copied, nullptr);
        if (p.copy) vkFreeCommandBuffers(device_, copy_pool_, 1, &p.copy);
        if (p.image) vkDestroyImage(device_, p.image, nullptr);
        if (p.memory) vkFreeMemory(device_, p.memory, nullptr);
        p = PacedPicture{};
    }
    paced_width_ = paced_height_ = 0;
}

bool VulkanDisplay::PresentImageAt(std::uint64_t image, int layout, std::uint32_t width, std::uint32_t height,
                                   std::uint64_t due_ns) {
    if (!width || !height || !image) return !closed_;
    if (closed_) return false;
    if (!EnsurePaced(width, height)) return false;
    // A picture neither waiting to be shown nor being shown - or, with all
    // of them waiting, the oldest waiting (its turn is lost: the shown
    // picture keeps no more than two frames behind). What the GPU still
    // reads of it (an earlier present's blit, submitted before) goes first,
    // by the queue's order and the copy's barrier.
    int slot = -1;
    {
        std::lock_guard<std::mutex> hold(paced_lock_);
        for (int i = 0; i < kPaced && slot < 0; ++i)
            if (i != taken_ && std::find(ready_.begin(), ready_.end(), i) == ready_.end()) slot = i;
        if (slot < 0) {
            slot = ready_.front();
            ready_.pop_front();
        }
    }
    PacedPicture& p = paced_[slot];
    vkWaitForFences(device_, 1, &p.copied, VK_TRUE, UINT64_MAX);    // its command buffer, from last time
    vkResetFences(device_, 1, &p.copied);
    vkResetCommandBuffer(p.copy, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(p.copy, &begin);
    const VkImage source = reinterpret_cast<VkImage>(image);
    const VkImageLayout kept = VkImageLayout(layout);
    Barrier(p.copy, source, kept, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    Barrier(p.copy, p.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    // A blit, not a copy: the renderer's targets are RGBA and these BGRA (a
    // copy takes the bytes as they are - red and blue swapped).
    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = region.srcSubresource;
    region.srcOffsets[1] = {std::int32_t(width), std::int32_t(height), 1};
    region.dstOffsets[1] = region.srcOffsets[1];
    vkCmdBlitImage(p.copy, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, p.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &region, VK_FILTER_NEAREST);
    Barrier(p.copy, p.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    Barrier(p.copy, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kept, VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    vkEndCommandBuffer(p.copy);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &p.copy;
    {
        std::lock_guard<std::mutex> hold(g_queue_lock);
        if (!Check(vkQueueSubmit(queue_, 1, &submit, p.copied), "vkQueueSubmit (paced)")) return false;
    }
    p.width = width;
    p.height = height;
    p.due_ns = due_ns;
    {
        std::lock_guard<std::mutex> hold(paced_lock_);
        ready_.push_back(slot);
    }
    paced_wake_.notify_all();
    return !closed_;
}

void VulkanDisplay::PresentLoop() {
    using Clock = std::chrono::steady_clock;
    for (;;) {
        int slot;
        {
            std::unique_lock<std::mutex> hold(paced_lock_);
            paced_wake_.wait(hold, [&] { return stop_ || !ready_.empty(); });
            if (stop_) return;
            slot = ready_.front();
        }
        // In order, each when it is due.
        const Clock::time_point due = std::min(Clock::time_point{std::chrono::nanoseconds(paced_[slot].due_ns)},
                                               Clock::now() + std::chrono::milliseconds(50));
        if (Clock::now() + std::chrono::milliseconds(2) < due)
            std::this_thread::sleep_until(due - std::chrono::milliseconds(1));
        while (Clock::now() < due) std::this_thread::yield();
        {
            std::lock_guard<std::mutex> hold(paced_lock_);
            if (stop_) return;
            if (ready_.empty() || ready_.front() != slot) continue;     // dropped meanwhile
            ready_.pop_front();
            taken_ = slot;
        }
        {
            std::lock_guard<std::mutex> hold(present_lock_);
            std::uint32_t index = 0;
            bool ok = true;
            if (BeginPresent(&index, &ok)) {
                FinishPresent(index, paced_[slot].image, paced_[slot].width, paced_[slot].height);
                Submit(index);
            }
        }
        {
            std::lock_guard<std::mutex> hold(paced_lock_);
            taken_ = -1;
        }
        paced_wake_.notify_all();
    }
}

bool VulkanDisplay::Submit(std::uint32_t index) {
    vkEndCommandBuffer(cmd_);
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &acquired_;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &rendered_[index];
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &rendered_[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &index;
    VkResult r;
    {
        std::lock_guard<std::mutex> hold(g_queue_lock);
        if (!Check(vkQueueSubmit(queue_, 1, &submit, fence_), "vkQueueSubmit")) return false;
        r = vkQueuePresentKHR(queue_, &present);
    }
    {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> hold(pacing_lock_);
        if (last_present_.time_since_epoch().count()) {
            const double ms = std::chrono::duration<double, std::milli>(now - last_present_).count();
            ++presents_;
            interval_sum_ += ms;
            interval_squares_ += ms * ms;
            interval_longest_ = std::max(interval_longest_, ms);
            static const bool hitch = std::getenv("PS2HOST_HITCH") != nullptr;
            if (hitch && ms > 25.0)
                std::fprintf(stderr, "[hitch] present: %.1f ms since the last\n", ms);
        }
        last_present_ = now;
    }
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) stale_ = true;
    else if (!Check(r, "vkQueuePresentKHR")) return false;
    return true;
}

Display::Pacing VulkanDisplay::TakePacing() {
    std::lock_guard<std::mutex> hold(pacing_lock_);
    Pacing p;
    p.presents = presents_;
    if (presents_) {
        p.mean_ms = interval_sum_ / double(presents_);
        p.jitter_ms = std::sqrt(std::max(0.0, interval_squares_ / double(presents_) - p.mean_ms * p.mean_ms));
        p.longest_ms = interval_longest_;
    }
    presents_ = 0;
    interval_sum_ = interval_squares_ = interval_longest_ = 0;
    return p;
}

void VulkanDisplay::SetVsync(bool on) {
    if (vsync_ == on) return;
    vsync_ = on;
    stale_ = true;                              // the swapchain again, with the other mode
}

void VulkanDisplay::SetFullscreen(bool on) {
    if (!window_) return;
    SDL_SetWindowFullscreen(window_, on);
    SDL_ShowCursor();
    if (on) SDL_HideCursor();
    stale_ = true;
}

bool VulkanDisplay::Fullscreen() const {
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

bool VulkanDisplay::Pump() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) closed_ = true;
        if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) stale_ = true;
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_RETURN &&
            (e.key.mod & SDL_KMOD_ALT))
            SetFullscreen(!Fullscreen());
        if (e.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad_) {
            gamepad_ = SDL_OpenGamepad(e.gdevice.which);
            if (gamepad_) std::fprintf(stderr, "[window] controller: %s\n", SDL_GetGamepadName(gamepad_));
        }
        if (e.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad_ && SDL_GetGamepadID(gamepad_) == e.gdevice.which) {
            std::fprintf(stderr, "[window] controller unplugged\n");
            SDL_CloseGamepad(gamepad_);
            gamepad_ = nullptr;
        }
    }
    return !closed_;
}

bool VulkanDisplay::Pad(PadState* state) const {
    if (!gamepad_) return false;
    // SDL's buttons by position (south is the DualShock's cross) to
    // DualShock 2 inputs.
    static const std::pair<SDL_GamepadButton, int> kButtons[] = {
        {SDL_GAMEPAD_BUTTON_BACK, 0}, {SDL_GAMEPAD_BUTTON_LEFT_STICK, 1}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, 2},
        {SDL_GAMEPAD_BUTTON_START, 3}, {SDL_GAMEPAD_BUTTON_DPAD_UP, 4}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 5},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 6}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 7},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 10}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 11},
        {SDL_GAMEPAD_BUTTON_NORTH, 12}, {SDL_GAMEPAD_BUTTON_EAST, 13}, {SDL_GAMEPAD_BUTTON_SOUTH, 14},
        {SDL_GAMEPAD_BUTTON_WEST, 15}};
    PadState s;
    for (const auto& [button, input] : kButtons)
        if (SDL_GetGamepadButton(gamepad_, button)) s.buttons |= std::uint16_t(1u << input);
    // Triggers (0-32767) are L2 and R2: pressed past a quarter of their travel.
    const int l2 = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    const int r2 = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    s.l2 = std::uint8_t(std::clamp(l2, 0, 32767) >> 7);
    s.r2 = std::uint8_t(std::clamp(r2, 0, 32767) >> 7);
    if (l2 > 8192) s.buttons |= 1u << 8;
    if (r2 > 8192) s.buttons |= 1u << 9;
    // Sticks: -32768-32767 to 0-255, down and right the larger, as the
    // DualShock's; a small dead zone reads as centred.
    static const SDL_GamepadAxis kAxes[4] = {SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
                                             SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY};
    for (int i = 0; i < 4; ++i) {
        const int v = SDL_GetGamepadAxis(gamepad_, kAxes[i]);
        s.sticks[i] = (v > -4096 && v < 4096) ? 0x80 : std::uint8_t((v + 32768) >> 8);
    }
    *state = s;
    return true;
}

bool VulkanDisplay::Focused() const {
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

bool VulkanDisplay::KeyDown(int scancode) const {
    int n = 0;
    const bool* keys = SDL_GetKeyboardState(&n);
    return keys && scancode >= 0 && scancode < n && keys[scancode];
}

VulkanDisplay::~VulkanDisplay() {
    {
        std::lock_guard<std::mutex> hold(paced_lock_);
        stop_ = true;
    }
    paced_wake_.notify_all();
    if (present_thread_.joinable()) present_thread_.join();
    shared_.reset();
    if (device_) vkDeviceWaitIdle(device_);
    DestroyPaced();
    if (copy_pool_) vkDestroyCommandPool(device_, copy_pool_, nullptr);
    DestroyPicture();
    DestroySwapchain();
    if (device_) {
        for (int i = 0; i < kInFlight; ++i) {
            if (fences_[i]) vkDestroyFence(device_, fences_[i], nullptr);
            if (acquireds_[i]) vkDestroySemaphore(device_, acquireds_[i], nullptr);
        }
        if (pool_) vkDestroyCommandPool(device_, pool_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
    if (gamepad_) SDL_CloseGamepad(gamepad_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

}  // namespace

std::unique_ptr<Display> CreateVulkanDisplay(const char* title, int width, int height, std::string* error) {
    auto display = std::make_unique<VulkanDisplay>();
    if (!display->Init(title, width, height)) {
        if (error) *error = display->error();
        return nullptr;
    }
    return display;
}

}  // namespace ps2gpu
