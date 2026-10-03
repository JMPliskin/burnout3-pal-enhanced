/*
 * The IOP's module loader (server 0x80000006, loadfile), on the host.
 *
 * Nothing runs on an IOP here: what a module would provide, the host provides,
 * one RPC server at a time. So loading a module succeeds without running
 * anything - it is noted, given a module ID, and reported as having started
 * and stayed resident - and the servers it would have registered answer from
 * the host when the game binds them (or are noted as missing).
 *
 * The protocol, as Burnout 3's EE-side library (0x00113578-0x00113F98) speaks it:
 *   procedure 0xFF  the module's version, 4 characters: the library compares it
 *                   with the release it was built for and refuses to load
 *                   anything otherwise. "2800" is SDK release 2.8.
 *   procedure 0     load a module from a path. The request:
 *                     0x000  argument length       (the answer: module ID)
 *                     0x004  -                     (the answer: start result)
 *                     0x008  path, 252 bytes ("cdrom0:\\IOP\\USBD.IRX;1")
 *                     0x104  arguments, 252 bytes
 * Any other procedure is noted and fails.
 */

#include <cstring>
#include <string>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/run.h"

namespace ps2host {

namespace {

constexpr char kVersion[4] = {'2', '8', '0', '0'};
constexpr s32 kNoSuchProcedure = -1;
constexpr u32 kResidentEnd = 0;     // a module's start result: started, stays loaded

std::vector<u8> Words(s32 first, u32 second) {
    std::vector<u8> out(8);
    std::memcpy(&out[0], &first, 4);
    std::memcpy(&out[4], &second, 4);
    return out;
}

std::vector<u8> LoadFile(u32 procedure, const std::vector<u8>& data) {
    static s32 next_id = 1;
    if (procedure == 0xFF) return std::vector<u8>(kVersion, kVersion + 4);
    if (procedure == 0 && data.size() >= 0x104) {
        const char* begin = reinterpret_cast<const char*>(&data[8]);
        const std::string path(begin, strnlen(begin, 252));
        const s32 id = next_id++;
        Note("module " + path, "IOP: module " + path + " loaded as " + std::to_string(id) +
                                   " (its services are the host's)");
        return Words(id, kResidentEnd);
    }
    Note("loadfile " + std::to_string(procedure),
         "IOP: loadfile procedure " + std::to_string(procedure) + " is not implemented (failed)");
    return Words(kNoSuchProcedure, 0);
}

}  // namespace

void InstallLoadFile() { RegisterRpcServer(0x80000006u, LoadFile); }

}  // namespace ps2host
