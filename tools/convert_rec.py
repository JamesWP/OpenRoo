#!/usr/bin/env python3
"""Convert version 1 input recordings to version 2.

    python3 tools/convert_rec.py tests/recordings/*.rec     # in place

Version 1 indexed its key array by DirectInput scan code (256 bytes) and
polled single keys by Windows virtual key; version 2 uses SDL scancodes for
both (512 bytes, and a 16-bit key in each poll).  See src/testing/record.cpp.
Files already at version 2 are left alone.  A poll of a key with no SDL
equivalent in the game's vocabulary (the old text entry also polled '[' and
':') is dropped; the new code never asks for it.
"""
import struct
import sys

HEADER = 80
KEYS_V1, KEYS_V2 = 256, 512

# DirectInput scan code (0x80 added for extended keys) -> SDL scancode.
DIK = {
    0x01: 41, 0x02: 30, 0x03: 31, 0x04: 32, 0x05: 33, 0x06: 34, 0x07: 35,
    0x08: 36, 0x09: 37, 0x0a: 38, 0x0b: 39, 0x0c: 45, 0x0d: 46, 0x0e: 42,
    0x0f: 43, 0x10: 20, 0x11: 26, 0x12: 8, 0x13: 21, 0x14: 23, 0x15: 28,
    0x16: 24, 0x17: 12, 0x18: 18, 0x19: 19, 0x1a: 47, 0x1b: 48, 0x1c: 40,
    0x1d: 224, 0x1e: 4, 0x1f: 22, 0x20: 7, 0x21: 9, 0x22: 10, 0x23: 11,
    0x24: 13, 0x25: 14, 0x26: 15, 0x27: 51, 0x28: 52, 0x29: 53, 0x2a: 225,
    0x2b: 49, 0x2c: 29, 0x2d: 27, 0x2e: 6, 0x2f: 25, 0x30: 5, 0x31: 17,
    0x32: 16, 0x33: 54, 0x34: 55, 0x35: 56, 0x36: 229, 0x37: 85, 0x38: 226,
    0x39: 44, 0x3a: 57, 0x3b: 58, 0x3c: 59, 0x3d: 60, 0x3e: 61, 0x3f: 62,
    0x40: 63, 0x41: 64, 0x42: 65, 0x43: 66, 0x44: 67, 0x45: 83, 0x46: 71,
    0x47: 95, 0x48: 96, 0x49: 97, 0x4a: 86, 0x4b: 92, 0x4c: 93, 0x4d: 94,
    0x4e: 87, 0x4f: 89, 0x50: 90, 0x51: 91, 0x52: 98, 0x53: 99, 0x56: 100,
    0x57: 68, 0x58: 69, 0x9c: 88, 0x9d: 228, 0xb5: 84, 0xb7: 70, 0xb8: 230,
    0xc5: 72, 0xc7: 74, 0xc8: 82, 0xc9: 75, 0xcb: 80, 0xcd: 79, 0xcf: 77,
    0xd0: 81, 0xd1: 78, 0xd2: 73, 0xd3: 76, 0xdb: 227, 0xdc: 231, 0xdd: 101,
}

# Windows virtual key -> SDL scancode, for the keys the game polls.
VK = {0x08: 42, 0x09: 43, 0x0d: 40, 0x10: 225, 0x1b: 41, 0x20: 44,
      0x25: 80, 0x26: 82, 0x27: 79, 0x28: 81}
VK.update({0x41 + i: 4 + i for i in range(26)})                  # A..Z
VK.update({0x31 + i: 30 + i for i in range(9)})                  # 1..9
VK[0x30] = 39                                                    # 0
VK.update({0x70: 58, 0x71: 59, 0x72: 60, 0x73: 61})              # F1..F4


def convert(blob):
    out = bytearray(blob[:HEADER])
    struct.pack_into("<I", out, 4, 2)
    off, dropped, unmapped = HEADER, 0, set()
    while off + 4 + 1 + KEYS_V1 + 1 <= len(blob):
        frame, = struct.unpack_from("<I", blob, off)
        state = blob[off + 4]
        keys = blob[off + 5:off + 5 + KEYS_V1]
        n = blob[off + 5 + KEYS_V1]
        off += 6 + KEYS_V1
        polls = [(blob[off + 2 * i], blob[off + 2 * i + 1]) for i in range(n)]
        off += 2 * n
        new_keys = bytearray(KEYS_V2)
        for sc, v in enumerate(keys):
            if v & 0x80:
                if sc not in DIK:
                    unmapped.add(sc)
                else:
                    new_keys[DIK[sc]] = v
        kept = []
        for vk, down in polls:
            if vk in VK:
                kept.append((VK[vk], down))
            else:
                dropped += 1
        out += struct.pack("<IB", frame, state) + new_keys + bytes([len(kept)])
        for key, down in kept:
            out += struct.pack("<HB", key, down)
    if off != len(blob):
        sys.exit("trailing bytes: not a version 1 recording?")
    return bytes(out), dropped, unmapped


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        blob = open(path, "rb").read()
        if blob[:4] != b"KROO":
            sys.exit("%s: not a recording" % path)
        ver, = struct.unpack_from("<I", blob, 4)
        if ver == 2:
            print("%s: already version 2" % path)
            continue
        if ver != 1:
            sys.exit("%s: version %d" % (path, ver))
        new, dropped, unmapped = convert(blob)
        if unmapped:
            sys.exit("%s: held scan codes with no SDL key: %s"
                     % (path, sorted(hex(s) for s in unmapped)))
        open(path, "wb").write(new)
        print("%s: converted (%d unused polls dropped)" % (path, dropped))


if __name__ == "__main__":
    main()
