/*
 * PlayStation 2 Emotion Engine kernel interface.
 *
 * These are the EE kernel services reached through the `syscall` instruction,
 * with the service number in $v1 and arguments in $a0..$a3.  The numbering is
 * fixed by the console's BIOS, not by the game, so these declarations describe
 * the platform rather than anything recovered from the executable.
 */

#ifndef PS2_KERNEL_H
#define PS2_KERNEL_H

#include "ps2rt/types.h"

namespace ps2 {

/* Semaphore service numbers, as used by the EE kernel. */
enum SyscallNumber : s32 {
    kSysCreateSema  = 0x40,
    kSysDeleteSema  = 0x41,
    kSysSignalSema  = 0x42,
    kSysISignalSema = 0x43,
    kSysWaitSema    = 0x44,
    kSysPollSema    = 0x45,
    kSysIPollSema   = 0x46,
};

/* Bit 16 of the COP0 Status register: the master interrupt enable. */
constexpr u32 kStatusEIE = 1u << 16;

/*
 * Release one count on a semaphore.  Returns the semaphore id on success and a
 * negative value on failure, following the EE kernel convention.
 */
s32 SignalSema(s32 sema_id);

/*
 * Enable interrupts, reporting whether they were already enabled.
 *
 * The return value is the *previous* state, which is what lets callers restore
 * the prior setting rather than unconditionally leaving interrupts on.
 */
s32 EnableIntr();

}  // namespace ps2

#endif  /* PS2_KERNEL_H */
