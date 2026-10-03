/*
 * The IOP's controller manager, dbcman (server 0x80001300), on the host. The
 * EE's libdbc reaches controllers through it, so this is where the host's
 * input will come in; so far it answers what libdbc needs to start.
 *
 * The protocol, as Burnout 3's libdbc (0x00215F18-0x002163C0) speaks it:
 *   procedure 0x80001363  the module's version, in the answer's first word as
 *                         major << 8 | minor. libdbc 3.0 refuses to work with
 *                         any major version but 3 ("Module version mismatch").
 * Any other procedure is noted and answered with zeros.
 */

#include <cstdio>
#include <string>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/run.h"

namespace ps2host {

namespace {

constexpr u32 kVersion = 0x0300;    // dbcman 3.0, as libdbc 3.0 expects

std::vector<u8> Dbc(u32 procedure, const std::vector<u8>& data) {
    std::vector<u8> answer(data.size() < 4 ? 4 : data.size(), 0);
    if (procedure == 0x80001363u) {
        answer[0] = u8(kVersion);
        answer[1] = u8(kVersion >> 8);
        return answer;
    }
    char text[96];
    std::snprintf(text, sizeof text, "IOP: dbcman procedure 0x%08X is not implemented (zeros)",
                  unsigned(procedure));
    Note("dbcman " + std::to_string(procedure), text);
    return answer;
}

}  // namespace

void InstallDbc() { RegisterRpcServer(0x80001300u, Dbc); }

}  // namespace ps2host
