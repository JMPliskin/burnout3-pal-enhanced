/*
 * The game disc, read from the user's own image of it: 2048-byte sectors and the
 * ISO 9660 file system on them, as the CD/DVD services the game calls (through
 * the IOP) need them. Nothing from the disc is part of a port.
 */

#ifndef PS2HOST_DISC_H
#define PS2HOST_DISC_H

#include <cstdio>
#include <string>
#include <vector>

#include "ps2rt/types.h"

namespace ps2host {

constexpr u32 kSectorSize = 2048;

class Disc {
public:
    struct File {
        u32 lsn = 0;        // first sector
        u32 size = 0;       // bytes
    };

    ~Disc();

    /* Open an image file (an .iso). */
    bool Open(const std::string& path);
    /* Use an image held in memory instead (the host's own tests). */
    void Use(std::vector<u8> image);
    bool IsOpen() const { return file_ != nullptr || !memory_.empty(); }

    /* Read `count` sectors from `lsn`; false past the end of the disc. */
    bool Read(u32 lsn, u32 count, u8* out);

    /* Look a path up - "\\DATA\\CARS.BIN;1" as the SDK writes it; separators may
       be either slash, the version suffix is optional, case does not matter. */
    bool Find(const std::string& path, File* out);
    /* A whole file's bytes (a path as Find takes it); false if it is not there. */
    bool ReadFile(const std::string& path, std::vector<u8>* out);
    /* The executable the console boots: SYSTEM.CNF's BOOT2 line, as a path on
       the disc ("\\SLUS_210.50;1"); "" when the image has none (not a PS2 disc). */
    std::string BootPath();

private:
    std::FILE* file_ = nullptr;
    std::vector<u8> memory_;
    u64 size_ = 0;
};

/* The disc the running game reads. */
Disc& GuestDisc();

}  // namespace ps2host

#endif  /* PS2HOST_DISC_H */
