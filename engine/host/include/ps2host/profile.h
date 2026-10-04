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

enum class Section { GsDraw, GsUpload, Vu1, Vu0, GpuBridge, GpuRender, GpuSync, Count };

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

}  // namespace ps2host

#endif  /* PS2HOST_PROFILE_H */
