/*
 * The IOP's controller manager, dbcman (server 0x80001300), on the host: the
 * devices behind the SDK's libdbc and, above it, libpad2. One DualShock 2 is
 * attached on port 0, driven by the keyboard while the window has focus.
 *
 * The protocol, as Burnout 3's copies of libdbc (0x00215F18-0x00216D20) and
 * libpad2 (0x00215620-0x00215EE0) speak it:
 *
 *   0x80001363  version: answer word 0 = major << 8 | minor (libdbc 3.0 needs 3)
 *   0x80001304  sceDbcSetWorkAddr: +4 the EE work area; its first 16 words are
 *               each socket's status, 1 when a device is there
 *   0x80001301  sceDbcCreateSocket: +8 the port, +0x28 and +0x2C the EE
 *               addresses of the socket's two 0x80-byte data blocks; answer
 *               +0x24 the socket (-1: none)
 *   0x80001302  sceDbcDeleteSocket: +0 the socket
 *   0x80001303  sceDbcGetDepNumber: +0 the socket; answer +4 (0: present)
 *
 * The device writes its state into the socket's blocks, the newer one
 * marked by the larger counter at +0x7C. A block:
 *   +0 state (1: connected)   +2 data length   +3 profile length
 *   +4 status (non-zero: valid)   +0x1C the data, then the profile
 * The profile is a bitmask of 40 inputs; libpad2 lays the data out by it
 * (0x00215E30): inputs 16-31 and 35-38 are a byte each (analog), the others
 * a bit each, in input order. A DualShock 2 has inputs 0-31: 16 buttons
 * (active low: a pressed button's bit is 0) then 16 analog bytes - 18 bytes,
 * profile FF FF FF FF 00.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"

namespace ps2host {

namespace {

constexpr u32 kVersion = 0x0300;    // dbcman 3.0, as libdbc 3.0 expects
constexpr u32 kDataBytes = 18, kProfileBytes = 5;

struct Socket {
    u32 port = 0, block[2] = {0, 0};
    u32 counter = 0;
};

struct Dbc {
    u32 work = 0;
    std::map<s32, Socket> sockets;
    s32 next = 0;
};

Dbc& D() {
    static Dbc dbc;
    return dbc;
}

u32 At(const std::vector<u8>& data, std::size_t offset) {
    u32 value = 0;
    if (offset + 4 <= data.size()) std::memcpy(&value, &data[offset], 4);
    return value;
}

void Put(std::vector<u8>& data, std::size_t offset, u32 value) {
    if (offset + 4 <= data.size()) std::memcpy(&data[offset], &value, 4);
}

// The keyboard as a DualShock 2: (input, key). Inputs 0-15 are the buttons in
// libpad's order: Select, L3, R3, Start, Up, Right, Down, Left, L2, R2, L1, R1,
// Triangle, Circle, Cross, Square.
const std::pair<int, int> kKeys[] = {
    {0, VK_BACK}, {3, VK_RETURN}, {4, VK_UP}, {5, VK_RIGHT}, {6, VK_DOWN}, {7, VK_LEFT},
    {8, '1'}, {9, '3'}, {10, 'Q'}, {11, 'E'}, {12, 'V'}, {13, 'C'}, {14, 'X'}, {15, 'Z'},
};

bool Pressed(int key) { return WindowFocused() && (GetAsyncKeyState(key) & 0x8000) != 0; }

void PadData(u8* data) {
    u16 buttons = 0xFFFF;                                   // active low: nothing pressed
    for (const auto& [input, key] : kKeys)
        if (Pressed(key)) buttons &= u16(~(1u << input));
    data[0] = u8(buttons);
    data[1] = u8(buttons >> 8);
    for (u32 i = 2; i < kDataBytes; ++i) data[i] = 0;      // pressures released
    // The analog sticks: centred.
    for (u32 i = 10; i < 14; ++i) data[i] = 0x80;
}

std::vector<u8> Server(u32 procedure, const std::vector<u8>& request) {
    Dbc& d = D();
    std::vector<u8> answer(request.size() < 0x30 ? 0x30 : request.size(), 0);
    std::copy(request.begin(), request.end(), answer.begin());
    switch (procedure) {
        case 0x80001363u:
            Put(answer, 0, kVersion);
            return answer;
        case 0x80001304u:
            d.work = At(request, 4);
            return answer;
        case 0x80001301u: {
            Socket s;
            s.port = At(request, 8);
            s.block[0] = At(request, 0x28);
            s.block[1] = At(request, 0x2C);
            const s32 id = s.port == 0 ? d.next++ % 16 : -1;   // a pad on port 0 only
            if (id >= 0) d.sockets[id] = s;
            char text[128];
            std::snprintf(text, sizeof text, "DBC: socket %d on port %u (blocks 0x%08X, 0x%08X)%s",
                          int(id), unsigned(s.port), unsigned(s.block[0]), unsigned(s.block[1]),
                          id < 0 ? ": nothing there" : "");
            Note("dbc socket " + std::to_string(s.port), text);
            Put(answer, 0x24, u32(id));
            return answer;
        }
        case 0x80001302u:
            d.sockets.erase(s32(At(request, 0)));
            return answer;
        case 0x80001303u:
            Put(answer, 4, d.sockets.count(s32(At(request, 0))) ? 0 : u32(-1));
            return answer;
        case 0x8000131Au: {
            // sceDbcReceiveData: +0 socket, +4 command, +8 room. Answer: +0x20C
            // the result (< 0: failed), +8 the length, +0xC the data - whose
            // first byte libpad2 reads as the device's state (1: connected).
            if (answer.size() < 0x210) answer.resize(0x210, 0);
            const bool there = d.sockets.count(s32(At(request, 0))) != 0;
            Put(answer, 0x20C, there ? 0 : u32(-1));
            Put(answer, 8, there ? 1 : 0);
            answer[0xC] = there ? 1 : 0;
            return answer;
        }
        default: {
            char text[96];
            std::snprintf(text, sizeof text, "IOP: dbcman procedure 0x%08X is not implemented (zeros)",
                          unsigned(procedure));
            Note("dbcman " + std::to_string(procedure), text);
            std::fill(answer.begin(), answer.end(), 0);
            return answer;
        }
    }
}

}  // namespace

// Each frame: every socket's device writes its state into the next block.
void DbcFrame() {
    Dbc& d = D();
    Memory& m = GuestMemory();
    for (auto& [id, s] : d.sockets) {
        if (d.work) m.Write(d.work + 4 * u32(id), 1, 4);
        const u32 b = s.block[(s.counter + 1) & 1];
        if (!b) continue;
        u8 data[kDataBytes];
        PadData(data);
        m.Write(b + 0, 1, 1);                           // connected
        m.Write(b + 1, 0, 1);
        m.Write(b + 2, kDataBytes, 1);
        m.Write(b + 3, kProfileBytes, 1);
        m.Write(b + 4, 1, 4);                           // valid
        for (u32 i = 0; i < kDataBytes; ++i) m.Write(b + 0x1C + i, data[i], 1);
        const u8 profile[kProfileBytes] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00};
        for (u32 i = 0; i < kProfileBytes; ++i) m.Write(b + 0x1C + kDataBytes + i, profile[i], 1);
        m.Write(b + 0x7C, ++s.counter, 4);
    }
}

void InstallDbc() { RegisterRpcServer(0x80001300u, Server); }

}  // namespace ps2host
