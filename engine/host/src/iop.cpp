/*
 * The IOP, as the EE sees it over SIF: high-level emulation of the I/O
 * processor's side of the SIF command and RPC protocol.
 *
 * The EE's SDK sends commands with SifSetDma: a 16-byte header - packet size,
 * extra-data size and address, command id - and the command's fields. The IOP
 * answers by DMA-ing a packet into the EE's receive buffer, whose address the EE
 * sent first (command 0x80000000), and raising the SIF0 DMA interrupt; the
 * game's own handler reads it from there. Here the answer is written into that
 * buffer and the interrupt raised by the scheduler, between threads, as the
 * hardware's would arrive.
 *
 * RPC: binding to a server (0x80000009) and calling one (0x8000000A) are
 * answered with an RPC-end packet (0x80000008). A call runs a host
 * implementation of the server, if there is one, with the data the EE sent;
 * what it returns is written to the EE's receive buffer. A server with no host
 * implementation still binds, so the game goes on; its calls are noted - server
 * and procedure - so the report says which IOP services the game needs.
 */

#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/iop_vm.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"

namespace ps2host {

namespace {

constexpr u32 kChangeSaddr = 0x80000000u, kSetSreg = 0x80000001u;
constexpr u32 kRpcEnd = 0x80000008u, kRpcBind = 0x80000009u, kRpcCall = 0x8000000Au;
constexpr u32 kSif0Channel = 5;
// Where the host pretends each bound server lives in IOP memory: never read by
// the EE, only compared with zero (bound or not) and handed back on calls.
constexpr u32 kServerBase = 0x00180000u;

struct Iop {
    u32 receive_buffer = 0;              // the EE's, from command 0x80000000
    std::map<u32, u32> sregs;            // software registers, command 0x80000001
    std::map<u32, u32> servers;          // server id -> pretended IOP address
    std::deque<std::vector<u8>> replies; // packets for the EE, not yet delivered
};

Iop& I() {
    static Iop iop;
    return iop;
}

std::map<u32, RpcServer>& Servers() {
    static std::map<u32, RpcServer> servers;
    return servers;
}

u32 Word(const std::vector<u8>& bytes, std::size_t at) {
    u32 value = 0;
    if (at + 4 <= bytes.size()) std::memcpy(&value, bytes.data() + at, 4);
    return value;
}

void PutWord(std::vector<u8>& bytes, std::size_t at, u32 value) {
    if (bytes.size() < at + 4) bytes.resize(at + 4);
    std::memcpy(bytes.data() + at, &value, 4);
}

std::vector<u8> ReadGuest(u32 address, u32 size) {
    std::vector<u8> bytes(size);
    Memory& m = GuestMemory();
    for (u32 i = 0; i < size; ++i) bytes[i] = u8(m.Read(address + i, 1));
    return bytes;
}

// An RPC-end packet answering `request`: its header fields, then client, the
// command it answers, the server, and the server's receive buffers.
void QueueEnd(const std::vector<u8>& request, u32 answered, u32 server) {
    std::vector<u8> packet(0x40, 0);
    PutWord(packet, 0, 0x40);                   // packet size; no extra data
    PutWord(packet, 8, kRpcEnd);
    PutWord(packet, 16, Word(request, 16));     // rec_id
    PutWord(packet, 20, Word(request, 20));     // pkt_addr
    PutWord(packet, 24, Word(request, 24));     // rpc_id
    PutWord(packet, 28, Word(request, 28));     // client
    PutWord(packet, 32, answered);
    PutWord(packet, 36, server);
    PutWord(packet, 40, server + 0x100);        // the server's receive buffer
    PutWord(packet, 44, server + 0x200);        // and its command buffer
    I().replies.push_back(std::move(packet));
    RequestDelivery();
}

std::string Hex(u32 value) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%08X", value);
    return text;
}

void Command(const std::vector<u8>& packet, const std::vector<u8>& data) {
    Iop& iop = I();
    const u32 command = Word(packet, 8);
    switch (command) {
        case kChangeSaddr:
            iop.receive_buffer = Word(packet, 16);
            Note("sif saddr", "SIF: the EE receives at " + Hex(iop.receive_buffer));
            return;
        case kSetSreg:
            iop.sregs[Word(packet, 16)] = Word(packet, 20);
            return;
        case kRpcBind: {
            const u32 sid = Word(packet, 32);
            u32& server = iop.servers[sid];
            if (!server) server = kServerBase + 0x400 * u32(iop.servers.size());
            Note("bind " + Hex(sid), "SIF: bound to IOP server " + Hex(sid) +
                 (Servers().count(sid) ? "" : " (no host implementation yet)"));
            QueueEnd(packet, kRpcBind, server);
            return;
        }
        case kRpcCall: {
            // struct SifRpcCallPkt: header, rec_id, pkt_addr, rpc_id, client,
            // rpc_number, send_size, receive, recv_size, rmode, server
            const u32 procedure = Word(packet, 32), receive = Word(packet, 40);
            const u32 receive_size = Word(packet, 44), server = Word(packet, 52);
            u32 sid = 0;
            for (const auto& [id, address] : iop.servers)
                if (address == server) sid = id;
            std::vector<u8> reply;
            auto implemented = Servers().find(sid);
            if (implemented != Servers().end()) {
                reply = implemented->second(procedure, data);
            } else {
                // Who called: the library function behind the RPC, for naming it.
                std::string callers;
                const std::vector<u32>& calls = CurrentCalls();
                for (std::size_t i = calls.size() > 4 ? calls.size() - 4 : 0; i < calls.size(); ++i)
                    callers += " " + Hex(calls[i]);
                Note("call " + Hex(sid) + " " + std::to_string(procedure),
                     "SIF: call to IOP server " + Hex(sid) + ", procedure " +
                     std::to_string(procedure) + ", " + std::to_string(data.size()) +
                     " bytes sent (no host implementation: answered with zeros); from" + callers);
            }
            Memory& m = GuestMemory();
            for (u32 i = 0; i < receive_size && receive; ++i)
                m.Write(receive + i, i < reply.size() ? reply[i] : 0, 1);
            QueueEnd(packet, kRpcCall, server);
            return;
        }
        default:
            if (!(command & 0x80000000u)) {
                // A command for a module's own handler (sceSifAddCmdHandler on
                // the IOP): run it there, if a module the host runs added one.
                if (iop::HasCommandHandler(command)) {
                    iop::Command(command, packet);
                    return;
                }
                Note("sif user " + Hex(command), "SIF: command " + Hex(command) +
                     " for a module the host does not run (dropped)");
                return;
            }
            Note("sif command " + Hex(command), "SIF: command " + Hex(command) + " (not handled)");
            return;
    }
}

}  // namespace

void RegisterRpcServer(u32 sid, RpcServer server) { Servers()[sid] = std::move(server); }

// SifSetDma: the transfers the EE queues for the IOP. A command packet is the
// one that starts with a header naming a SIF command; a transfer before it in
// the same list is the command's extra data (an RPC call's arguments).
void SifDma(u32 transfers, u32 count) {
    Memory& m = GuestMemory();
    std::vector<u8> data;
    for (u32 i = 0; i < count; ++i) {
        // struct SifDmaTransfer { void* src; void* dest; int size; int attr; }
        const u32 t = transfers + 16 * i;
        const u32 src = u32(m.Read(t, 4)), dest = u32(m.Read(t + 4, 4));
        const u32 size = u32(m.Read(t + 8, 4));
        std::vector<u8> bytes = ReadGuest(src, size);
        const u32 command = Word(bytes, 8);
        // A command packet's header gives its own size; user commands (below
        // 0x80000000) are a module's, system ones the SIF library's.
        const bool user = command < 0x100u;
        if (size >= 16 && (command & 0x80000000u || user) && (Word(bytes, 0) & 0xFF) == size) {
            Command(bytes, data);
            data.clear();
        } else {
            // Extra data, DMA'd into IOP memory where the sender said - a
            // module reads it there. Data for the host's own servers is bound
            // for their pretended addresses, above any module's memory.
            if (dest < kServerBase)
                for (u32 k = 0; k < size; ++k) m.Write(kIopRamBase + dest + k, bytes[k], 1);
            data = std::move(bytes);
        }
    }
}

void QueueIopPacket(std::vector<u8> packet) {
    I().replies.push_back(std::move(packet));
    RequestDelivery();
}

// Deliver one queued answer: into the EE's receive buffer, then the SIF0 DMA
// interrupt. False when none is waiting.
bool DeliverIopReply() {
    Iop& iop = I();
    if (iop.replies.empty() || !iop.receive_buffer) return false;
    std::vector<u8> packet = std::move(iop.replies.front());
    iop.replies.pop_front();
    Memory& m = GuestMemory();
    for (std::size_t i = 0; i < packet.size(); ++i) m.Write(iop.receive_buffer + u32(i), packet[i], 1);
    RaiseDmac(kSif0Channel);
    return true;
}

}  // namespace ps2host
