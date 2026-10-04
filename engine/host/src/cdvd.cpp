/*
 * The CD/DVD services the IOP's file system module (cdvdfsv) provides to the EE,
 * as host RPC servers reading the user's disc image (disc.cpp).
 *
 * Each server's protocol is read from the game's own EE-side library, which
 * builds the request and reads the answer: Burnout 3's copy of the SDK's
 * libcdvd is at 0x0011E9A8-0x0011FD00. The servers are added one at a time, as
 * a game is seen to reach them.
 */

#include <cstdio>
#include <ctime>
#include <cstring>
#include <string>
#include <vector>

#include "ps2host/disc.h"
#include "ps2host/host.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"

namespace ps2host {

namespace {

std::vector<u8> Word(u32 value) {
    return {u8(value), u8(value >> 8), u8(value >> 16), u8(value >> 24)};
}

u32 At(const std::vector<u8>& data, std::size_t offset) {
    u32 value = 0;
    if (offset + 4 <= data.size()) std::memcpy(&value, &data[offset], 4);
    return value;
}

// sceCdDiskReady (server 0x8000059C, procedure 0): sent the wait mode, answers
// the drive's state in one word - 2, SCECdComplete, when a disc is ready, and 6,
// SCECdNotReady, when there is none.
std::vector<u8> DiskReady(u32 /*procedure*/, const std::vector<u8>& /*data*/) {
    return Word(GuestDisc().IsOpen() ? 2 : 6);
}

// sceCdSearchFile (server 0x80000597, procedure 0). The request (0x12C bytes):
//   0x000  sceCdlFILE { u32 lsn; u32 size; char name[16]; u8 date[8]; }
//   0x024  the path, 256 bytes, NUL-terminated: "\\DATA\\CARS.BIN;1"
//   0x124  the EE address of this buffer, where the answer's file record goes
// The answer word is 1 when the file was found, 0 when not; the library then
// copies the record from the buffer to its caller.
std::vector<u8> SearchFile(u32 /*procedure*/, const std::vector<u8>& data) {
    if (data.size() < 0x128) return Word(0);
    const char* begin = reinterpret_cast<const char*>(&data[0x24]);
    const std::string path(begin, strnlen(begin, 256));
    Disc::File file;
    const bool found = GuestDisc().IsOpen() && GuestDisc().Find(path, &file);
    Note("cd search " + path, "CD: search " + path + (found ? "" : " (not found)"));
    if (!found) return Word(0);
    const u32 record = At(data, 0x124);
    Memory& m = GuestMemory();
    m.Write(record, file.lsn, 4);
    m.Write(record + 4, file.size, 4);
    // The name, as the directory has it: the path's last part.
    const std::size_t slash = path.find_last_of("\\/");
    const std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    for (u32 i = 0; i < 16; ++i)
        m.Write(record + 8 + i, i < name.size() ? u8(name[i]) : 0, 1);
    for (u32 i = 0; i < 8; ++i) m.Write(record + 24 + i, 0, 1);
    return Word(1);
}

u8 Bcd(int value) { return u8(((value / 10) << 4) | (value % 10)); }

// S-commands (server 0x80000593): the procedure is the command; each answers a
// result word, 1 for success, and the command's data after it. Read from the
// game's wrappers, each of which names its command and reply size.
std::vector<u8> SCommand(u32 procedure, const std::vector<u8>& /*data*/) {
    switch (procedure) {
        case 0x01: {                                    // sceCdReadClock: result, sceCdCLOCK
            // With --virtual-time the clock starts at a fixed date and follows
            // the virtual clock: games seed their random choices from it
            // (Burnout 3 its EA Trax playlist), and a scripted run must repeat.
            std::tm local{};
            if (VirtualClock()) {
                const std::time_t start = 1094558400;   // 2004-09-07 12:00:00 UTC, Burnout 3's release
                const std::time_t now = start + std::time_t(ClockNs() / 1000000000ull);
                gmtime_s(&local, &now);
            } else {
                const std::time_t now = std::time(nullptr);
                localtime_s(&local, &now);
            }
            std::vector<u8> reply = Word(1);
            // sceCdCLOCK { stat, second, minute, hour, pad, day, month, year }, BCD
            const u8 clock[8] = {0, Bcd(local.tm_sec), Bcd(local.tm_min), Bcd(local.tm_hour), 0,
                                 Bcd(local.tm_mday), Bcd(local.tm_mon + 1),
                                 Bcd(local.tm_year % 100)};
            reply.insert(reply.end(), clock, clock + 8);
            return reply;
        }
        case 0x03:                                      // sceCdGetDiskType
            return Word(GuestDisc().IsOpen() ? 0x14 : 0x00);   // SCECdPS2DVD, or none
        case 0x05: {                                    // sceCdTrayReq: result, tray state
            std::vector<u8> reply = Word(1);
            const std::vector<u8> closed = Word(0);
            reply.insert(reply.end(), closed.begin(), closed.end());
            return reply;
        }
        case 0x22:                                      // sceCdMmode: the media mode is set
            return Word(1);
        default:
            Note("cd scmd " + std::to_string(procedure),
                 "CD: S-command " + std::to_string(procedure) + " (answered with zeros)");
            return {};
    }
}

}  // namespace

void InstallIopServers() {
    InstallFileIo();
    InstallLoadFile();
    InstallDbc();
    InstallIopHeap();
    RegisterRpcServer(0x8000059Cu, DiskReady);
    RegisterRpcServer(0x80000597u, SearchFile);
    RegisterRpcServer(0x80000593u, SCommand);
}

}  // namespace ps2host
