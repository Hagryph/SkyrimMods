from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path


IMAGE_BASE_DEFAULT = 0x140000000


@dataclass
class Section:
    name: str
    va: int
    vsize: int
    raw: int
    raw_size: int
    data: bytes


@dataclass
class Hit:
    va: int
    kind: str
    offset: int
    base: str
    mnemonic: str
    bytes_hex: str


REGS = ("rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15")
PREFIX_BYTES = {0x66, 0x67, 0xF2, 0xF3}


def u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def parse_pe(path: Path) -> tuple[int, list[Section]]:
    data = path.read_bytes()
    pe_off = u32(data, 0x3C)
    if data[pe_off : pe_off + 4] != b"PE\0\0":
        raise ValueError("not a PE file")

    coff = pe_off + 4
    section_count = u16(data, coff + 2)
    opt_size = u16(data, coff + 16)
    opt = coff + 20
    magic = u16(data, opt)
    if magic != 0x20B:
        raise ValueError("expected PE32+")
    image_base = struct.unpack_from("<Q", data, opt + 24)[0]

    sections: list[Section] = []
    sec_off = opt + opt_size
    for i in range(section_count):
        off = sec_off + i * 40
        name = data[off : off + 8].split(b"\0", 1)[0].decode("ascii", errors="replace")
        vsize = u32(data, off + 8)
        va = u32(data, off + 12)
        raw_size = u32(data, off + 16)
        raw = u32(data, off + 20)
        sections.append(Section(name, va, vsize, raw, raw_size, data[raw : raw + raw_size]))
    return image_base or IMAGE_BASE_DEFAULT, sections


def read_modrm_disp(buf: bytes, index: int, rex: int | None) -> tuple[int | None, str, int]:
    if index >= len(buf):
        return None, "?", index
    modrm = buf[index]
    index += 1
    mod = (modrm >> 6) & 3
    rm = modrm & 7
    rex_b = 8 if rex is not None and (rex & 0x1) else 0
    if mod == 3:
        return None, "reg", index

    if rm == 4:
        if index >= len(buf):
            return None, "?", index
        sib = buf[index]
        index += 1
        base = sib & 7
        base_name = REGS[base | rex_b]
        if mod == 0 and base == 5:
            disp = struct.unpack_from("<i", buf, index)[0]
            return disp, "rip", index + 4
        if mod == 0:
            return 0, base_name, index

    if mod == 0:
        if rm == 5:
            disp = struct.unpack_from("<i", buf, index)[0]
            return disp, "rip", index + 4
        return 0, REGS[rm | rex_b], index
    if mod == 1:
        disp = struct.unpack_from("<b", buf, index)[0]
        return disp, REGS[rm | rex_b], index + 1
    if mod == 2:
        disp = struct.unpack_from("<i", buf, index)[0]
        return disp, REGS[rm | rex_b], index + 4
    return None, "?", index


def scan_section(image_base: int, section: Section, offsets: set[int], no_stack: bool) -> list[Hit]:
    buf = section.data
    hits: list[Hit] = []
    i = 0
    while i < len(buf):
        start = i
        if start > 0 and (buf[start - 1] in PREFIX_BYTES or 0x40 <= buf[start - 1] <= 0x4F):
            i += 1
            continue
        prefixes: list[int] = []
        rex = None
        while i < len(buf):
            b = buf[i]
            if b in (0x66, 0x67, 0xF2, 0xF3) or 0x40 <= b <= 0x4F:
                prefixes.append(b)
                if 0x40 <= b <= 0x4F:
                    rex = b
                i += 1
                continue
            break
        if i >= len(buf):
            break

        prefix_set = set(prefixes)
        op = buf[i]
        i += 1
        mnemonic = None
        kind = None
        modrm_index = None
        imm_len = 0

        if op in (0x88, 0x89):
            mnemonic = "mov r/m,reg"
            kind = "store"
            modrm_index = i
        elif op in (0xC6, 0xC7):
            if i < len(buf) and ((buf[i] >> 3) & 7) == 0:
                mnemonic = "mov r/m,imm"
                kind = "store"
                modrm_index = i
                imm_len = 1 if op == 0xC6 else 4
        elif op == 0x8D:
            mnemonic = "lea"
            kind = "address"
            modrm_index = i
        elif op == 0x0F and i < len(buf):
            op2 = buf[i]
            i += 1
            if op2 in (0x11, 0x29):
                mnemonic = "xmm store"
                kind = "store"
                modrm_index = i
            elif op2 == 0x7F and (0x66 in prefix_set or 0xF3 in prefix_set):
                mnemonic = "xmm aligned/unaligned store"
                kind = "store"
                modrm_index = i
            elif op2 in (0x13, 0x17) or (op2 == 0xD6 and 0x66 in prefix_set):
                mnemonic = "xmm partial store"
                kind = "store"
                modrm_index = i

        if modrm_index is None:
            i = start + 1
            continue

        disp, base, after = read_modrm_disp(buf, modrm_index, rex)
        if disp is None:
            i = start + 1
            continue
        if no_stack and base in ("rsp", "rbp"):
            i = start + 1
            continue
        if after + imm_len > len(buf):
            i = start + 1
            continue

        disp_unsigned = disp & 0xFFFFFFFF
        if disp in offsets or disp_unsigned in offsets:
            end = after + imm_len
            va = image_base + section.va + start
            hits.append(Hit(va, kind or "unknown", disp & 0xFFFFFFFF, base, mnemonic or "unknown", buf[start:end].hex()))

        i = start + 1
    return hits


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("pe", type=Path)
    ap.add_argument("--offset", action="append", required=True)
    ap.add_argument("--section", default=".text")
    ap.add_argument("--no-stack", action="store_true")
    ap.add_argument("--out", type=Path, required=True)
    ns = ap.parse_args()

    offsets = {int(v, 0) for v in ns.offset}
    image_base, sections = parse_pe(ns.pe)
    all_hits: list[Hit] = []
    for sec in sections:
        if sec.name.lower() != ns.section.lower():
            continue
        all_hits.extend(scan_section(image_base, sec, offsets, ns.no_stack))

    all_hits.sort(key=lambda h: (h.offset, h.va, h.kind))
    lines = [
        f"imageBase=0x{image_base:x}",
        "offsets=" + ", ".join(f"0x{o:x}" for o in sorted(offsets)),
        f"hits={len(all_hits)}",
        "",
    ]
    for h in all_hits:
        lines.append(f"0x{h.va:x} offset=0x{h.offset:x} base={h.base} {h.kind} {h.mnemonic} bytes={h.bytes_hex}")
    ns.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {ns.out} hits={len(all_hits)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
