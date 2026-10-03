/* The Vulkan objects behind ps2gpu::Gpu, for the library's own sources. */

#ifndef PS2GPU_GPU_INTERNAL_H
#define PS2GPU_GPU_INTERNAL_H

#include <volk.h>

#include <string>

#include "ps2gpu/gs_renderer.h"

namespace ps2gpu {

class VulkanGpu final : public Gpu {
public:
    ~VulkanGpu() override;
    bool Init(std::string* error);
    std::string Describe() const override { return describe_; }

    std::uint32_t FindMemory(std::uint32_t bits, VkMemoryPropertyFlags wanted) const;
    /* Records with `record`, submits, and waits for it to finish. */
    template <typename F>
    bool Run(F&& record);

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

private:
    std::string describe_;
};

template <typename F>
bool VulkanGpu::Run(F&& record) {
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    record(cmd);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkResetFences(device, 1, &fence);
    if (vkQueueSubmit(queue, 1, &submit, fence) != VK_SUCCESS) return false;
    return vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
}

}  // namespace ps2gpu

#endif  /* PS2GPU_GPU_INTERNAL_H */
