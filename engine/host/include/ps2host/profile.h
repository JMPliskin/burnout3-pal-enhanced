/*
 * Where the host's time goes, for the report: a few sections timed with the
 * steady clock, exclusively (a section entered inside another pauses the
 * outer one, so VU1's time does not include the GS drawing its XGKICKs cause).
 * Cheap enough to stay on: a clock read on entry and exit.
 */

#ifndef PS2HOST_PROFILE_H
#define PS2HOST_PROFILE_H

#include <string>

namespace ps2host {

enum class Section { GsDraw, GsUpload, Vu1, Vu0, GpuBridge, GpuRender, GpuSync, Dma, Iop, Spu2, Count };

class Timed {
public:
    explicit Timed(Section section);
    ~Timed();
    Timed(const Timed&) = delete;
    Timed& operator=(const Timed&) = delete;

private:
    Section section_;
};

/* "GS drawing 61.2%, ..." of the run's wall time so far. */
std::string ProfileSummary();

/* --sample: a sampling profiler on the calling thread (the EE's, which runs
   the translated code, the host's devices and the GPU bridge): a thousand
   times a second, the function it is in and seven callers. At the
   end, the functions most often found running ("self"), and those most
   often on the stack at all ("total"), by name from the executable's
   symbols. Windows only (elsewhere it reports nothing). */
void StartSampler();
std::string SampleReport(int top = 30);

}  // namespace ps2host

#endif  /* PS2HOST_PROFILE_H */
