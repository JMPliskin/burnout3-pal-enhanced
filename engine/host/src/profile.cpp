/* Timed sections for the report (profile.h). */

#include "ps2host/profile.h"

#include <chrono>
#include <cstdio>

namespace ps2host {

namespace {

using Clock = std::chrono::steady_clock;

const Clock::time_point g_start = Clock::now();
double g_seconds[int(Section::Count)] = {};
// The sections open now, innermost last, and when the innermost resumed.
Section g_stack[16];
int g_depth = 0;
Clock::time_point g_since;

double Elapsed(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double>(to - from).count();
}

}  // namespace

Timed::Timed(Section section) : section_(section) {
    const Clock::time_point now = Clock::now();
    if (g_depth > 0) g_seconds[int(g_stack[g_depth - 1])] += Elapsed(g_since, now);
    if (g_depth < 16) g_stack[g_depth] = section;
    ++g_depth;
    g_since = now;
}

Timed::~Timed() {
    const Clock::time_point now = Clock::now();
    g_seconds[int(section_)] += Elapsed(g_since, now);
    --g_depth;
    g_since = now;
}

std::string ProfileSummary() {
    static const char* const kNames[] = {"GS drawing", "GS image transfers", "VU1", "VU0"};
    const double total = Elapsed(g_start, Clock::now());
    std::string out = "time:";
    char text[64];
    std::snprintf(text, sizeof text, " %.1f s;", total);
    out += text;
    for (int i = 0; i < int(Section::Count); ++i) {
        std::snprintf(text, sizeof text, " %s %.1f%%", kNames[i], total > 0 ? 100.0 * g_seconds[i] / total : 0.0);
        out += text;
        if (i + 1 < int(Section::Count)) out += ",";
    }
    return out;
}

}  // namespace ps2host
