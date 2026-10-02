#include "ps2host/memory.h"

#include <cstring>

#include "ps2host/run.h"
#include "ps2rt/generated.h"

namespace ps2host {

namespace {

// Hooks are indexed by register word: the EE window's words, then the GS's.
constexpr u32 kEeWords = kEeRegisterSize / 4;
constexpr u32 kGsWords = kGsRegisterSize / 4;

// kseg0 and kseg1 addresses, as the physical address they reach.
u32 Physical(u32 address) {
    return (address >= 0x80000000u && address < 0xC0000000u) ? address & 0x1FFFFFFFu : address;
}

int HookIndex(u32 address) {
    if (address - kEeRegisterBase < kEeRegisterSize) return (address - kEeRegisterBase) / 4;
    if (address - kGsRegisterBase < kGsRegisterSize)
        return kEeWords + (address - kGsRegisterBase) / 4;
    return -1;
}

}  // namespace

Memory::Memory()
    : rdram_(kRdramSize), scratchpad_(kScratchpadSize), ee_registers_(kEeRegisterSize),
      vu_memory_(kVuMemorySize), gs_registers_(kGsRegisterSize), iop_ram_(kIopRamSize),
      bios_(kBiosSize),
      read_hooks_(kEeWords + kGsWords), write_hooks_(kEeWords + kGsWords) {}

u8* Memory::Translate(u32 address, u32 size) {
    // kseg0 (0x80000000) and kseg1 (0xA0000000) see the whole low 512 MB of the
    // physical address space - RDRAM and the device registers alike.
    address = Physical(address);
    const u32 last = address + size - 1;
    // RDRAM and its user-mode mirrors: the top bits select the window, the rest
    // the byte.
    switch (address >> 28) {
        case 0x0: case 0x2: case 0x3: {
            const u32 offset = address & 0x0FFFFFFFu;
            if (offset < kRdramSize && (last & 0x0FFFFFFFu) < kRdramSize)
                return rdram_.data() + offset;
            break;
        }
        default:
            break;
    }
    struct Window { u32 base, size; std::vector<u8>* bytes; };
    Window windows[] = {
        {kScratchpadBase, kScratchpadSize, &scratchpad_},
        {kEeRegisterBase, kEeRegisterSize, &ee_registers_},
        {kVuMemoryBase, kVuMemorySize, &vu_memory_},
        {kGsRegisterBase, kGsRegisterSize, &gs_registers_},
        {kIopRamBase, kIopRamSize, &iop_ram_},
        {kBiosBase, kBiosSize, &bios_},
    };
    for (const Window& w : windows) {
        if (address - w.base < w.size && last - w.base < w.size)
            return w.bytes->data() + (address - w.base);
    }
    return nullptr;
}

u64 Memory::Read(u32 address, u32 size) {
    address = Physical(address);
    u8* bytes = Translate(address, size);
    if (!bytes) throw MemoryFault{address, false};
    u64 value = 0;
    std::memcpy(&value, bytes, size);        // the EE and the host are both little-endian
    const int index = HookIndex(address & ~3u);
    if (index >= 0) {
        RunStats().register_reads[address & ~3u]++;
        if (read_hooks_[index]) value = read_hooks_[index](address, value);
    }
    return value;
}

void Memory::Write(u32 address, u64 value, u32 size) {
    address = Physical(address);
    u8* bytes = Translate(address, size);
    if (!bytes) throw MemoryFault{address, true};
    if (address >= kBiosBase && address - kBiosBase < kBiosSize) return;   // ROM
    std::memcpy(bytes, &value, size);
    const int index = HookIndex(address & ~3u);
    if (index >= 0) {
        RunStats().register_writes[address & ~3u]++;
        if (write_hooks_[index]) write_hooks_[index](address, value);
    }
}

void Memory::OnRead(u32 address, ReadHook hook) {
    const int index = HookIndex(address & ~3u);
    if (index >= 0) read_hooks_[index] = std::move(hook);
}

void Memory::OnWrite(u32 address, WriteHook hook) {
    const int index = HookIndex(address & ~3u);
    if (index >= 0) write_hooks_[index] = std::move(hook);
}

Memory& GuestMemory() {
    static Memory memory;
    return memory;
}

}  // namespace ps2host

// The hooks generated and lifted code read and write guest memory through.
namespace generated {

namespace {

u64 Load(u32 address, u32 size, bool is_signed) {
    u64 value = ps2host::GuestMemory().Read(address, size);
    if (is_signed && size < 8) {
        const int shift = 64 - 8 * int(size);
        value = u64(s64(value << shift) >> shift);
    }
    return value;
}

}  // namespace

u64 ReadS8(u32 a) { return Load(a, 1, true); }
u64 ReadU8(u32 a) { return Load(a, 1, false); }
u64 ReadS16(u32 a) { return Load(a, 2, true); }
u64 ReadU16(u32 a) { return Load(a, 2, false); }
u64 ReadS32(u32 a) { return Load(a, 4, true); }
u64 ReadU32(u32 a) { return Load(a, 4, false); }
u64 ReadU64(u32 a) { return Load(a, 8, false); }
void Write8(u32 a, u32 v) { ps2host::GuestMemory().Write(a, v, 1); }
void Write16(u32 a, u32 v) { ps2host::GuestMemory().Write(a, v, 2); }
void Write32(u32 a, u32 v) { ps2host::GuestMemory().Write(a, v, 4); }
void Write64(u32 a, u64 v) { ps2host::GuestMemory().Write(a, v, 8); }

}  // namespace generated
