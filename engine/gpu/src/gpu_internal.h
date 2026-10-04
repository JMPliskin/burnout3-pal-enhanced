/* The Vulkan objects behind ps2gpu::Gpu, for the library's own sources. */

#ifndef PS2GPU_GPU_INTERNAL_H
#define PS2GPU_GPU_INTERNAL_H

#include <volk.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ps2gpu/gs_renderer.h"

namespace ps2gpu {

class VulkanGpu final : public Gpu {
public:
    ~VulkanGpu() override;
    bool Init(std::string* error);
    /* On a device someone else made and keeps (a Display's): only the command
       pool and fence are this one's own. */
    bool InitShared(VkInstance instance, VkPhysicalDevice gpu, VkDevice device, std::uint32_t family, VkQueue queue,
                    bool dual_source, const std::string& describe, std::string* error);
    std::string Describe() const override { return describe_; }

    std::uint32_t FindMemory(std::uint32_t bits, VkMemoryPropertyFlags wanted) const;
    /* Records into the open command buffer (opening one if none is), to run
       at the next Submit: a frame's work goes to the GPU in one submission. */
    template <typename F>
    void Record(F&& record);
    /* Runs what has been recorded and waits for it; then what was waiting for
       that (AfterSubmit: freeing staging buffers, say) runs. */
    bool Submit();
    void AfterSubmit(std::function<void()> done) { after_submit_.push_back(std::move(done)); }
    bool recording() const { return open_; }
    /* Records with `record`, submits, and waits for it to finish. */
    template <typename F>
    bool Run(F&& record) {
        Record(std::forward<F>(record));
        return Submit();
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool dual_source = false;       /* dualSrcBlend: the GS's blending */
    bool owns_device = true;

private:
    std::string describe_;
    bool open_ = false;
    std::vector<std::function<void()>> after_submit_;
};

template <typename F>
void VulkanGpu::Record(F&& record) {
    if (!open_) {
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);
        open_ = true;
    }
    record(cmd);
}

inline bool VulkanGpu::Submit() {
    bool ok = true;
    if (open_) {
        open_ = false;
        vkEndCommandBuffer(cmd);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vkResetFences(device, 1, &fence);
        ok = vkQueueSubmit(queue, 1, &submit, fence) == VK_SUCCESS &&
             vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    }
    std::vector<std::function<void()>> done;
    done.swap(after_submit_);
    for (auto& f : done) f();
    return ok;
}

}  // namespace ps2gpu

#endif  /* PS2GPU_GPU_INTERNAL_H */
