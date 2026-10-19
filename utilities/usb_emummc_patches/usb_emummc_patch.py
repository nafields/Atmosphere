#!/usr/bin/env python3
# Copyright (c) Atmosphère-NX
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""
Author the `usb` sysmodule patches that a USB emuMMC boot needs.

On a USB emuMMC boot the emummc module owns the XUSB host controller, and the
`usb` sysmodule (0100000000000006) must not reset or take the controller.
loader applies IPS patches from

    /atmosphere/usb_emummc_patches/<patchset name>/<nso build id>.ips

to the `usb` main NSO, on USB emuMMC boots only. The patches are specific to a
Horizon version (one per build id) and are authored from the firmware's own
`usb` main NSO, which this tool reads. The NSO is not distributed with
Atmosphère; extract it from the installed firmware (for example with hactool:
`hactool -t nca --exefsdir=out <usb program nca>`, then `out/main`).

Subcommands:

    info        <nso>                  build id, segment layout, compression
    decompress  <nso> -o <out>         write the NSO uncompressed, in loader's
                                       layout (segment file offset = 0x100 +
                                       memory offset), which is the layout IPS
                                       offsets are relative to
    find        <nso>                  list code that materialises XUSB register
                                       addresses and data that holds them, with
                                       the enclosing function start; the
                                       starting points for finding the host and
                                       device bring-up functions
    make        <nso> -o <dir> --stub <text offset>[=<w0>] ...
                                       write <dir>/<build id>.ips that replaces
                                       the start of each listed function with
                                       `mov w0, #<w0>; ret` (default w0 = 0,
                                       success); --patch <text offset>:<hex>
                                       adds arbitrary bytes
    apply       <nso> <ips> -o <out>   apply an IPS to an NSO (for checking
                                       the result in a disassembler)

Offsets given to and printed by this tool are .text offsets (as a disassembler
shows them for an NSO loaded at 0). The IPS records they turn into use
0x100 + offset, as loader expects.
"""

import argparse
import os
import struct
import sys

NSO_MAGIC = b"NSO0"
NSO_HEADER_SIZE = 0x100
SEGMENT_NAMES = ("text", "rodata", "data")

# Tegra X1 XUSB register blocks, as the usb sysmodule maps them.
XUSB_REGIONS = (
    ("XUSB_HOST",      0x70090000, 0x8000),
    ("XUSB_HOST_FPCI", 0x70098000, 0x1000),
    ("XUSB_HOST_IPFS", 0x70099000, 0x1000),
    ("XUSB_PADCTL",    0x7009F000, 0x1000),
    ("XUSB_DEV",       0x700D0000, 0xA000),
)

IPS_HEAD = b"PATCH"
IPS_TAIL = b"EOF"
IPS32_HEAD = b"IPS32"
IPS32_TAIL = b"EEOF"
IPS_MAX_OFFSET = 0xFFFFFF
IPS_EOF_OFFSET = 0x454F46  # a record at this offset would read as the tail


class Nso:
    def __init__(self, data):
        if len(data) < NSO_HEADER_SIZE or data[0:4] != NSO_MAGIC:
            raise ValueError("not an NSO0 file")
        self.header = bytearray(data[:NSO_HEADER_SIZE])
        self.flags = struct.unpack_from("<I", data, 0x0C)[0]
        self.module_id = bytes(data[0x40:0x60])
        self.bss_size = struct.unpack_from("<I", data, 0x3C)[0]
        self.segments = []
        for i, name in enumerate(SEGMENT_NAMES):
            file_off, mem_off, size = struct.unpack_from("<III", data, 0x10 + 0x10 * i)
            comp_size = struct.unpack_from("<I", data, 0x60 + 4 * i)[0]
            compressed = bool(self.flags & (1 << i))
            raw = data[file_off:file_off + (comp_size if compressed else size)]
            if compressed:
                raw = self._lz4_decompress(raw, size)
            if len(raw) != size:
                raise ValueError("%s segment is truncated (%#x of %#x bytes)" % (name, len(raw), size))
            self.segments.append({"name": name, "mem_off": mem_off, "data": bytearray(raw)})

    @staticmethod
    def _lz4_decompress(raw, size):
        try:
            import lz4.block
        except ImportError:
            sys.exit("the NSO is compressed: install the lz4 module (pip install lz4) or decompress it first")
        return lz4.block.decompress(raw, uncompressed_size=size)

    @property
    def build_id(self):
        return self.module_id.hex().upper()

    @property
    def build_id_short(self):
        # loader accepts the build id with trailing zero bytes trimmed.
        return self.module_id.rstrip(b"\0").hex().upper()

    def segment(self, name):
        return next(s for s in self.segments if s["name"] == name)

    def write_uncompressed(self, path):
        """loader's layout: every segment at file offset 0x100 + memory offset."""
        header = bytearray(self.header)
        struct.pack_into("<I", header, 0x0C, self.flags & ~0x7)
        out = bytearray(header)
        for i, seg in enumerate(self.segments):
            file_off = NSO_HEADER_SIZE + seg["mem_off"]
            if len(out) < file_off:
                out += b"\0" * (file_off - len(out))
            out[file_off:file_off + len(seg["data"])] = seg["data"]
            struct.pack_into("<I", out, 0x10 + 0x10 * i, file_off)
            struct.pack_into("<I", out, 0x60 + 4 * i, len(seg["data"]))
        with open(path, "wb") as f:
            f.write(out)


def load_nso(path):
    with open(path, "rb") as f:
        return Nso(f.read())


# ---------------------------------------------------------------------------
# AArch64 helpers (only what the finder needs)

def insn_at(text, off):
    return struct.unpack_from("<I", text, off)[0]


def decode_movz_movk(insn):
    """Return (kind, is64, rd, imm16, shift) for MOVZ/MOVK, else None."""
    if (insn & 0x7F800000) == 0x52800000:
        kind = "movz"
    elif (insn & 0x7F800000) == 0x72800000:
        kind = "movk"
    else:
        return None
    is64 = bool(insn & 0x80000000)
    hw = (insn >> 21) & 3
    if not is64 and hw > 1:
        return None
    return kind, is64, insn & 0x1F, (insn >> 5) & 0xFFFF, hw * 16


def is_function_start(insn):
    if insn == 0xD503233F:                       # paciasp
        return True
    if (insn & 0xFFC003E0) == 0xA98003E0:        # stp xA, xB, [sp, #-imm]!
        return True
    if (insn & 0xFF8003FF) == 0xD10003FF:        # sub sp, sp, #imm
        return True
    return False


def find_function_start(text, off, limit=0x2000):
    """Nearest prologue at or above off; a `ret`/`b` right before it supports the guess."""
    start = off
    while start >= 0 and off - start <= limit:
        if is_function_start(insn_at(text, start)):
            prev = insn_at(text, start - 4) if start >= 4 else None
            ends_before = prev is None or prev == 0xD65F03C0 or (prev & 0xFC000000) == 0x14000000
            return start, ends_before
        start -= 4
    return None, False


def region_for(value):
    for name, base, size in XUSB_REGIONS:
        if base <= value < base + size:
            return name, value - base
    return None


def find_code_refs(text):
    """MOVZ/MOVK sequences that build an XUSB register address in one register."""
    hits = []
    for off in range(0, len(text) - 3, 4):
        d = decode_movz_movk(insn_at(text, off))
        if d is None or d[0] != "movz":
            continue
        _, is64, rd, imm, shift = d
        value = imm << shift
        hit = region_for(value & 0xFFFFFFFF)
        best = (off, value, hit) if hit else None
        # Fold the MOVKs that follow for the same register (a few unrelated instructions may sit between)
        # and keep the last value that lands in an XUSB block.
        scan = off + 4
        while scan < len(text) and scan - off <= 8 * 4:
            dk = decode_movz_movk(insn_at(text, scan))
            if dk is not None and dk[2] == rd:
                if dk[0] == "movz":
                    break
                _, _, _, imm_k, shift_k = dk
                value = (value & ~(0xFFFF << shift_k)) | (imm_k << shift_k)
                hit = region_for(value & 0xFFFFFFFF)
                if hit:
                    best = (scan, value, hit)
            scan += 4
        if best:
            end, value, hit = best
            hits.append((off, end, rd, is64, value, hit))
    return hits


def find_data_refs(nso):
    """32/64-bit words equal to an XUSB register address in any segment (IO mapping tables, literals)."""
    hits = []
    for seg in nso.segments:
        data = seg["data"]
        for off in range(0, len(data) - 3, 4):
            v32 = struct.unpack_from("<I", data, off)[0]
            r = region_for(v32)
            if r and (off % 8 or len(data) - off < 8 or struct.unpack_from("<Q", data, off)[0] != v32):
                hits.append((seg["name"], off, 32, v32, r))
            if off % 8 == 0 and len(data) - off >= 8:
                v64 = struct.unpack_from("<Q", data, off)[0]
                r = region_for(v64)
                if r:
                    hits.append((seg["name"], off, 64, v64, r))
    return hits


# ---------------------------------------------------------------------------
# IPS

def write_ips(records, path):
    """records: list of (nso file offset, bytes). Uses IPS, or IPS32 when needed."""
    records = sorted(records)
    for i in range(1, len(records)):
        prev_off, prev_data = records[i - 1]
        if prev_off + len(prev_data) > records[i][0]:
            raise ValueError("overlapping patch records at %#x" % records[i][0])
    use32 = any(off > IPS_MAX_OFFSET or off == IPS_EOF_OFFSET for off, _ in records)
    out = bytearray(IPS32_HEAD if use32 else IPS_HEAD)
    for off, data in records:
        while data:
            chunk, data = data[:0xFFFF], data[0xFFFF:]
            if use32:
                out += struct.pack(">IH", off, len(chunk))
            else:
                out += struct.pack(">I", off)[1:] + struct.pack(">H", len(chunk))
            out += chunk
            off += len(chunk)
    out += IPS32_TAIL if use32 else IPS_TAIL
    with open(path, "wb") as f:
        f.write(out)
    return use32


def read_ips(path):
    with open(path, "rb") as f:
        data = f.read()
    if data.startswith(IPS_HEAD):
        is32, tail, pos = False, IPS_TAIL, len(IPS_HEAD)
    elif data.startswith(IPS32_HEAD):
        is32, tail, pos = True, IPS32_TAIL, len(IPS32_HEAD)
    else:
        raise ValueError("not an IPS/IPS32 file")
    records = []
    while True:
        if data[pos:pos + len(tail)] == tail:
            break
        if is32:
            off, size = struct.unpack_from(">IH", data, pos)
            pos += 6
        else:
            off = int.from_bytes(data[pos:pos + 3], "big")
            size = struct.unpack_from(">H", data, pos + 3)[0]
            pos += 5
        if size == 0:  # RLE record
            rle_size, value = struct.unpack_from(">HB", data, pos)
            pos += 3
            records.append((off, bytes([value]) * rle_size))
        else:
            records.append((off, data[pos:pos + size]))
            pos += size
    return records


def stub_bytes(w0):
    if not 0 <= w0 <= 0xFFFF:
        raise ValueError("stub return value must fit in 16 bits")
    movz_w0 = 0x52800000 | (w0 << 5)
    ret = 0xD65F03C0
    return struct.pack("<II", movz_w0, ret)


def parse_int(s):
    return int(s, 0)


# ---------------------------------------------------------------------------
# Commands

def cmd_info(args):
    nso = load_nso(args.nso)
    print("build id      : %s" % nso.build_id)
    print("patch file    : %s.ips" % nso.build_id_short)
    print("flags         : %#x (%s)" % (nso.flags, ", ".join(
        "%s compressed" % n for i, n in enumerate(SEGMENT_NAMES) if nso.flags & (1 << i)) or "uncompressed"))
    for seg in nso.segments:
        print("%-7s        : memory %#08x size %#08x -> ips offset base %#x" % (
            seg["name"], seg["mem_off"], len(seg["data"]), NSO_HEADER_SIZE + seg["mem_off"]))
    print("bss           : size %#x" % nso.bss_size)


def cmd_decompress(args):
    nso = load_nso(args.nso)
    nso.write_uncompressed(args.output)
    print("wrote %s (build id %s)" % (args.output, nso.build_id))


def cmd_find(args):
    nso = load_nso(args.nso)
    text = nso.segment("text")["data"]
    print("build id %s, .text %#x bytes" % (nso.build_id, len(text)))
    code = find_code_refs(text)
    print("\n%d code references (MOVZ/MOVK building an XUSB register address):" % len(code))
    funcs = {}
    for off, end, rd, is64, value, (region, delta) in code:
        start, clean = find_function_start(text, off)
        reg = ("x" if is64 else "w") + str(rd)
        where = "function %#x%s" % (start, "" if clean else " (uncertain prologue)") if start is not None else "no prologue found"
        print("  .text+%#08x..%#08x  %-3s = %#010x  %s+%#x  in %s" % (off, end, reg, value, region, delta, where))
        if start is not None:
            funcs.setdefault(start, set()).add(region)
    if funcs:
        print("\nfunctions with XUSB register references (candidates, check each in a disassembler):")
        for start in sorted(funcs):
            print("  .text+%#08x  %s" % (start, ", ".join(sorted(funcs[start]))))
    data = find_data_refs(nso)
    print("\n%d data references (register addresses stored as 32/64-bit words, e.g. IO mapping tables):" % len(data))
    for seg, off, width, value, (region, delta) in data:
        print("  .%s+%#08x  u%d %#x  %s+%#x" % (seg, off, width, value, region, delta))
    if not code and not data:
        print("\nnothing found: is this the usb sysmodule's main NSO?")


def cmd_make(args):
    nso = load_nso(args.nso)
    text = nso.segment("text")["data"]
    records = []

    def add(text_off, data, what):
        if text_off % 4:
            raise SystemExit("%s: offset %#x is not 4-byte aligned" % (what, text_off))
        if text_off + len(data) > len(text):
            raise SystemExit("%s: offset %#x is outside .text (size %#x)" % (what, text_off, len(text)))
        records.append((NSO_HEADER_SIZE + text_off, data))

    for spec in args.stub or []:
        off_s, _, w0_s = spec.partition("=")
        off, w0 = parse_int(off_s), parse_int(w0_s) if w0_s else 0
        if not args.force and not is_function_start(insn_at(text, off)):
            raise SystemExit("--stub %#x: %08x does not look like a function prologue "
                             "(paciasp / stp ..., [sp, #-n]! / sub sp); pass --force to stub it anyway" % (off, insn_at(text, off)))
        add(off, stub_bytes(w0), "--stub %#x" % off)
    for spec in args.patch or []:
        off_s, _, hex_s = spec.partition(":")
        add(parse_int(off_s), bytes.fromhex(hex_s), "--patch %s" % off_s)
    if not records:
        raise SystemExit("nothing to patch: give --stub and/or --patch")

    os.makedirs(args.output, exist_ok=True)
    path = os.path.join(args.output, "%s.ips" % (nso.build_id if args.full_build_id else nso.build_id_short))
    use32 = write_ips(records, path)
    print("wrote %s (%s, %d records)" % (path, "IPS32" if use32 else "IPS", len(records)))
    for off, data in sorted(records):
        print("  file %#08x (.text+%#08x): %s" % (off, off - NSO_HEADER_SIZE, data.hex()))
    print("install as /atmosphere/usb_emummc_patches/<patchset name>/%s" % os.path.basename(path))


def cmd_apply(args):
    nso = load_nso(args.nso)
    text = nso.segment("text")
    for off, data in read_ips(args.ips):
        if off < NSO_HEADER_SIZE:
            raise SystemExit("record at %#x patches the NSO header, which loader refuses" % off)
        mem = off - NSO_HEADER_SIZE
        for seg in nso.segments:
            if seg["mem_off"] <= mem < seg["mem_off"] + len(seg["data"]):
                rel = mem - seg["mem_off"]
                seg["data"][rel:rel + len(data)] = data
                break
        else:
            raise SystemExit("record at %#x is outside every segment" % off)
    nso.write_uncompressed(args.output)
    print("wrote %s (build id %s, uncompressed, loader layout)" % (args.output, nso.build_id))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("info"); s.add_argument("nso"); s.set_defaults(func=cmd_info)
    s = sub.add_parser("decompress"); s.add_argument("nso"); s.add_argument("-o", "--output", required=True); s.set_defaults(func=cmd_decompress)
    s = sub.add_parser("find"); s.add_argument("nso"); s.set_defaults(func=cmd_find)
    s = sub.add_parser("make")
    s.add_argument("nso")
    s.add_argument("-o", "--output", required=True, help="directory for <build id>.ips")
    s.add_argument("--stub", action="append", metavar="OFFSET[=W0]", help=".text offset of a function to replace with `mov w0, #W0; ret`")
    s.add_argument("--patch", action="append", metavar="OFFSET:HEX", help=".text offset and bytes to write")
    s.add_argument("--force", action="store_true", help="stub an offset that does not look like a function start")
    s.add_argument("--full-build-id", action="store_true", help="name the file with all 32 build id bytes")
    s.set_defaults(func=cmd_make)
    s = sub.add_parser("apply"); s.add_argument("nso"); s.add_argument("ips"); s.add_argument("-o", "--output", required=True); s.set_defaults(func=cmd_apply)

    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
