/*
 * The CD/DVD services the IOP's file system module (cdvdfsv) provides to the EE,
 * as host RPC servers reading the user's disc image (disc.cpp).
 *
 * Each server's protocol is read from the game's own EE-side library, which
 * builds the request and reads the answer: Burnout 3's copy of the SDK's
 * libcdvd is at 0x0011E9A8-0x0011F8F0. The servers are added one at a time, as
 * a game is seen to reach them.
 */

#include <vector>

#include "ps2host/disc.h"
#include "ps2host/host.h"
#include "ps2host/run.h"

namespace ps2host {

namespace {

std::vector<u8> Word(u32 value) {
    return {u8(value), u8(value >> 8), u8(value >> 16), u8(value >> 24)};
}

// sceCdDiskReady (server 0x8000059C, procedure 0): sent the wait mode, answers
// the drive's state in one word - 2, SCECdComplete, when a disc is ready, and 6,
// SCECdNotReady, when there is none.
std::vector<u8> DiskReady(u32 /*procedure*/, const std::vector<u8>& /*data*/) {
    return Word(GuestDisc().IsOpen() ? 2 : 6);
}

}  // namespace

void InstallIopServers() {
    RegisterRpcServer(0x8000059Cu, DiskReady);
}

}  // namespace ps2host
