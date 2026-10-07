/* The Vulkan objects behind ps2gpu::Gpu, for the library's own sources. */

#ifndef PS2GPU_GPU_INTERNAL_H
#define PS2GPU_GPU_INTERNAL_H

#include <volk.h>

#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ps2gpu/gs_renderer.h"

namespace ps2gpu {

/* SubmitStats: submissions, the time spent waiting for the GPU, and what
   waited (a read-back of which kind, a resize, a full ring). */
inline std::uint64_t g_submits = 0;
inline double g_wait_seconds = 0;
struct WaitReason { const char* why; std::uint64_t count; double seconds; };
inline WaitReason g_waits[16] = {};
inline void CountWait(const char* why, double seconds) {
    for (WaitReason& w : g_waits) {
        if (!w.why) w.why = why;
        if (w.why == why || std::string(w.why) == why) {
            ++w.count;
            w.seconds += seconds;
            return;
        }
    }
}

class VulkanGpu final : public Gpu {
public:
    ~VulkanGpu() override;
    bool Init(std::string* error);
    /* On a device someone else made and keeps (a Display's): only the command
       pool, command buffers and fences are this one's own. */
    bool InitShared(VkInstance instance, VkPhysicalDevice gpu, VkDevice device, std::uint32_t family, VkQueue queue,
                    bool dual_source, const std::string& describe, std::string* error);
    std::string Describe() const override { return describe_; }

    std::uint32_t FindMemory(std::uint32_t bits, VkMemoryPropertyFlags wanted) const;
    /* Records into the open command buffer (opening one if none is), to run
       at the next Submit: a frame's work goes to the GPU in one submission.
       A rendering pass left open (RecordInPass) is ended first. */
    template <typename F>
    void Record(F&& record);
    /* The same, leaving a rendering pass a previous call left open: for draws
       that continue it (end_pass says how to end one). */
    template <typename F>
    void RecordInPass(F&& record);
    std::function<void(VkCommandBuffer)> end_pass;
    /* Runs what has been recorded and waits for it, and for everything
       Flush handed over before; then what was waiting for that (AfterSubmit:
       freeing staging buffers, say) runs. */
    bool Submit(const char* why = "other");
    /* Hands what has been recorded to the GPU without waiting for it, so that
       the GPU works while the CPU goes on: the next recording goes into the
       next of kSlots command buffers (waiting only if that one is still
       running). What waited on the work handed over runs once it has run. */
    bool Flush();
    void AfterSubmit(std::function<void()> done) { after_submit_.push_back(std::move(done)); }
    bool recording() const { return open_; }
    /* Counts command buffers begun: what was set in one is not in the next. */
    std::uint64_t recording_id() const { return recordings_; }
    static constexpr int kSlots = 3;
    /* The command buffer recording goes into (or will). */
    int slot() const { return current_; }
    /* Records with `record`, submits, and waits for it to finish. */
    template <typename F>
    bool Run(F&& record, const char* why = "other") {
        Record(std::forward<F>(record));
        return Submit(why);
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;           /* the current slot's */
    VkCommandBuffer cmds[kSlots] = {};
    VkFence fences[kSlots] = {};
    bool dual_source = false;       /* dualSrcBlend: the GS's blending */
    bool logic_op = false;          /* logicOp: FBMSK's partly masked channels */
    bool owns_device = true;

private:
    std::string describe_;
    bool open_ = false;
    std::uint64_t recordings_ = 0;
    std::vector<std::function<void()>> after_submit_;
    int current_ = 0;
    bool in_flight_[kSlots] = {};
    std::vector<std::function<void()>> slot_done_[kSlots];
    /* Waits for a slot's work, if handed over, and runs what waited on it;
       the time it waited goes to `why`. */
    bool Retire(int slot, const char* why);
};

template <typename F>
void VulkanGpu::RecordInPass(F&& record) {
    if (!open_) {
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);
        open_ = true;
        ++recordings_;
    }
    record(cmd);
}

template <typename F>
void VulkanGpu::Record(F&& record) {
    RecordInPass([&](VkCommandBuffer c) {
        if (end_pass) end_pass(c);
        record(c);
    });
}

inline bool VulkanGpu::Retire(int slot, const char* why) {
    if (!in_flight_[slot]) return true;
    const auto start = std::chrono::steady_clock::now();
    const bool ok = vkWaitForFences(device, 1, &fences[slot], VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    g_wait_seconds += waited;
    CountWait(why, waited);
    in_flight_[slot] = false;
    std::vector<std::function<void()>> done;
    done.swap(slot_done_[slot]);
    for (auto& f : done) f();
    return ok;
}

inline bool VulkanGpu::Flush() {
    if (!open_) return true;
    if (end_pass) end_pass(cmd);
    open_ = false;
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkResetFences(device, 1, &fences[current_]);
    const bool ok = vkQueueSubmit(queue, 1, &submit, fences[current_]) == VK_SUCCESS;
    ++g_submits;
    in_flight_[current_] = ok;
    slot_done_[current_].swap(after_submit_);
    if (!ok) {                                      // nothing will run: nothing to wait for
        for (auto& f : slot_done_[current_]) f();
        slot_done_[current_].clear();
    }
    current_ = (current_ + 1) % kSlots;
    cmd = cmds[current_];
    return Retire(current_, "a full ring") && ok;
}

inline bool VulkanGpu::Submit(const char* why) {
    bool ok = Flush();
    // Oldest first: current_ is free, the one after it was handed over first.
    for (int i = 1; i <= kSlots; ++i) ok = Retire((current_ + i) % kSlots, why) && ok;
    std::vector<std::function<void()>> done;        // waited on nothing recorded
    done.swap(after_submit_);
    for (auto& f : done) f();
    return ok;
}

}  // namespace ps2gpu

#endif  /* PS2GPU_GPU_INTERNAL_H */
