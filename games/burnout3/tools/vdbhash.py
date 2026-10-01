"""The ValueDB name hash, recovered from `func_0021BFA0`.

The tuning system keys every parameter by a 32-bit hash of its full
slash-separated path.  The routine is a table-driven reflected CRC32 using the
usual polynomial (0xEDB88320, verified against the 256-entry table the game
keeps at 0x0049B110), but it deviates from a textbook CRC32 in two ways that
have to be preserved:

* the accumulator is a **signed** int and the shift is arithmetic (`sra`), so the
  vacated high bits are filled with the sign bit rather than zeroes;
* there is **no final XOR**.

Both deviations are visible in the disassembly and both change the output, so
`zlib.crc32` is not a drop-in substitute.  `crc32_textbook` is provided purely so
the difference can be demonstrated.
"""

from __future__ import annotations

CRC_POLYNOMIAL = 0xEDB88320
TABLE_ADDRESS = 0x0049B110  # where the original keeps its precomputed table
HASH_ADDRESS = 0x0021BFA0   # the function this was recovered from


def _build_table() -> tuple:
    table = []
    for index in range(256):
        value = index
        for _ in range(8):
            value = (value >> 1) ^ CRC_POLYNOMIAL if value & 1 else value >> 1
        table.append(value)
    return tuple(table)


CRC_TABLE = _build_table()


def _sign_extend_32(value: int) -> int:
    """Interpret the low 32 bits of `value` as a signed 32-bit integer."""
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def hash_name(name) -> int:
    """Hash a name or path exactly as the game does.

    Accepts str or bytes.  A str is encoded as ASCII, which is what every name in
    the database actually is - the original uses a *signed* byte load, so a
    character above 0x7F would index outside the table.
    """
    data = name.encode("ascii") if isinstance(name, str) else bytes(name)
    crc = -1
    for byte in data:
        index = (byte ^ (crc & 0xFF)) & 0xFF
        # Python's >> on a negative int is already arithmetic, which is what the
        # original's `sra` does; re-sign-extend after the XOR so the next shift
        # sees a 32-bit value.
        crc = _sign_extend_32((crc >> 8) ^ CRC_TABLE[index])
    return crc & 0xFFFFFFFF


def crc32_textbook(name) -> int:
    """Standard reflected CRC32 with an unsigned accumulator and no final XOR.

    Present only for comparison: it differs from `hash_name` and will not
    reproduce the game's keys.
    """
    data = name.encode("ascii") if isinstance(name, str) else bytes(name)
    crc = 0xFFFFFFFF
    for byte in data:
        crc = (crc >> 8) ^ CRC_TABLE[(byte ^ (crc & 0xFF)) & 0xFF]
    return crc & 0xFFFFFFFF


def join_path(*parts) -> str:
    """Assemble a path the way `func_0021BB60` does.

    That function concatenates its string arguments and inserts a "/" only when
    the accumulated text does not already end with one.
    """
    out = ""
    for part in parts:
        if not part:
            continue
        if out and not out.endswith("/"):
            out += "/"
        out += part
    return out
