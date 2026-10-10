#include "ps2host/disc.h"

#include <cctype>
#include <cstring>

namespace ps2host {

namespace {

u32 Le32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }

std::string Upper(std::string s) {
    for (char& ch : s) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

// A directory record's name without its version (";1") and trailing dot.
std::string Bare(std::string name) {
    const auto semicolon = name.find(';');
    if (semicolon != std::string::npos) name.resize(semicolon);
    if (!name.empty() && name.back() == '.') name.pop_back();
    return Upper(name);
}

}  // namespace

Disc::~Disc() {
    if (file_) std::fclose(file_);
}

bool Disc::Open(const std::string& path) {
    if (file_) std::fclose(file_);
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) return false;
    _fseeki64(file_, 0, SEEK_END);
    size_ = u64(_ftelli64(file_));
    return true;
}

void Disc::Use(std::vector<u8> image) {
    memory_ = std::move(image);
    size_ = memory_.size();
}

bool Disc::Read(u32 lsn, u32 count, u8* out) {
    const u64 offset = u64(lsn) * kSectorSize, length = u64(count) * kSectorSize;
    if (offset + length > size_) return false;
    if (!memory_.empty()) {
        std::memcpy(out, memory_.data() + offset, size_t(length));
        return true;
    }
    if (!file_ || _fseeki64(file_, s64(offset), SEEK_SET) != 0) return false;
    return std::fread(out, 1, size_t(length), file_) == length;
}

bool Disc::Find(const std::string& path, File* out) {
    // The primary volume descriptor, sector 16, holds the root directory's record.
    std::vector<u8> sector(kSectorSize);
    if (!Read(16, 1, sector.data()) || sector[0] != 1 || std::memcmp(&sector[1], "CD001", 5) != 0)
        return false;
    File directory{Le32(&sector[156 + 2]), Le32(&sector[156 + 10])};
    std::string rest = path;
    for (char& ch : rest) if (ch == '/') ch = '\\';
    while (!rest.empty()) {
        while (!rest.empty() && rest[0] == '\\') rest.erase(0, 1);
        if (rest.empty()) break;
        const auto end = rest.find('\\');
        const std::string part = Bare(rest.substr(0, end));
        rest = end == std::string::npos ? "" : rest.substr(end);
        // Scan the directory's records for this part.
        const u32 sectors = (directory.size + kSectorSize - 1) / kSectorSize;
        std::vector<u8> data(size_t(sectors) * kSectorSize);
        if (!Read(directory.lsn, sectors, data.data())) return false;
        bool found = false;
        for (u32 at = 0; at < directory.size;) {
            const u8 length = data[at];
            if (length == 0) {                  // records never cross a sector boundary
                at = (at / kSectorSize + 1) * kSectorSize;
                continue;
            }
            const u8 name_length = data[at + 32];
            const std::string name(reinterpret_cast<const char*>(&data[at + 33]), name_length);
            if (name_length > 1 || (name_length == 1 && name[0] > 1)) {
                if (Bare(name) == part) {
                    directory = File{Le32(&data[at + 2]), Le32(&data[at + 10])};
                    found = true;
                    break;
                }
            }
            at += length;
        }
        if (!found) return false;
    }
    *out = directory;
    return true;
}

bool Disc::ReadFile(const std::string& path, std::vector<u8>* out) {
    File file;
    if (!Find(path, &file)) return false;
    out->assign(size_t(file.size + kSectorSize - 1) / kSectorSize * kSectorSize, 0);
    if (!Read(file.lsn, u32(out->size() / kSectorSize), out->data())) return false;
    out->resize(file.size);
    return true;
}

std::string Disc::BootPath() {
    std::vector<u8> cnf;
    if (!ReadFile("SYSTEM.CNF", &cnf)) return "";
    const std::string text(cnf.begin(), cnf.end());
    for (std::size_t at = 0; at < text.size();) {
        std::size_t end = text.find_first_of("\r\n", at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        // BOOT2 = cdrom0:\SLUS_210.50;1
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        std::string key = line.substr(0, equals);
        key.erase(key.find_last_not_of(" \t") + 1);
        if (Upper(key) != "BOOT2") continue;
        std::string value = line.substr(equals + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        const std::size_t colon = value.find(':');
        return colon == std::string::npos ? value : value.substr(colon + 1);
    }
    return "";
}

Disc& GuestDisc() {
    static Disc disc;
    return disc;
}

}  // namespace ps2host
