/*
 * VulkanGpu: an instance and a device with one graphics queue, no window.
 * Vulkan 1.3 for dynamic rendering (drawing without render pass objects).
 */

#include "gpu_internal.h"

#include <cstdio>
#include <vector>

namespace ps2gpu {

bool VulkanGpu::Init(std::string* error) {
    auto fail = [&](const char* what, VkResult r) {
        char text[160];
        std::snprintf(text, sizeof text, "%s failed (VkResult %d)", what, int(r));
        if (error) *error = text;
        return false;
    };
    VkResult r = volkInitialize();
    if (r != VK_SUCCESS) return fail("volkInitialize (is there a Vulkan driver?)", r);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "ps2gpu";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if ((r = vkCreateInstance(&ici, nullptr, &instance)) != VK_SUCCESS) return fail("vkCreateInstance", r);
    volkLoadInstance(instance);

    std::uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    std::vector<VkPhysicalDevice> list(n);
    vkEnumeratePhysicalDevices(instance, &n, list.data());
    int best = -1;
    for (VkPhysicalDevice candidate : list) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(candidate, &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        std::uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, props.data());
        for (std::uint32_t f = 0; f < families; ++f) {
            if (!(props[f].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            const int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score > best) {
                best = score;
                gpu = candidate;
                family = f;
            }
            break;
        }
    }
    if (!gpu) {
        if (error) *error = "no Vulkan 1.3 device with a graphics queue";
        return false;
    }
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(gpu, &p);
    describe_ = p.deviceName;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.dynamicRendering = VK_TRUE;
    v13.synchronization2 = VK_TRUE;
    // Dual-source blending: the GS's blend factor (source alpha or FIX, where
    // 0x80 is 1.0) is a second fragment output, not the alpha written.
    VkPhysicalDeviceFeatures supported;
    vkGetPhysicalDeviceFeatures(gpu, &supported);
    dual_source = supported.dualSrcBlend == VK_TRUE;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.features.dualSrcBlend = supported.dualSrcBlend;
    features.pNext = &v13;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &features;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if ((r = vkCreateDevice(gpu, &dci, nullptr, &device)) != VK_SUCCESS) return fail("vkCreateDevice", r);
    volkLoadDevice(device);
    vkGetDeviceQueue(device, family, 0, &queue);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = family;
    if ((r = vkCreateCommandPool(device, &pci, nullptr, &pool)) != VK_SUCCESS)
        return fail("vkCreateCommandPool", r);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if ((r = vkAllocateCommandBuffers(device, &cai, &cmd)) != VK_SUCCESS)
        return fail("vkAllocateCommandBuffers", r);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if ((r = vkCreateFence(device, &fci, nullptr, &fence)) != VK_SUCCESS) return fail("vkCreateFence", r);
    return true;
}

std::uint32_t VulkanGpu::FindMemory(std::uint32_t bits, VkMemoryPropertyFlags wanted) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
    return ~0u;
}

VulkanGpu::~VulkanGpu() {
    if (device) {
        vkDeviceWaitIdle(device);
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
}

std::unique_ptr<Gpu> CreateGpu(std::string* error) {
    auto gpu = std::make_unique<VulkanGpu>();
    if (!gpu->Init(error)) return nullptr;
    return gpu;
}

}  // namespace ps2gpu
