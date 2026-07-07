from __future__ import annotations

import argparse
import struct
from pathlib import Path

from pe_x64_field_scan import parse_pe


PREFIX_BYTES = {0x66, 0x67, 0xF2, 0xF3}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("pe", type=Path)
    ap.add_argument("--target", action="append", required=True)
    ap.add_argument("--section", default=".text")
    ap.add_argument("--out", type=Path, required=True)
    ns = ap.parse_args()

    targets = {int(v, 0) for v in ns.target}
    image_base, sections = parse_pe(ns.pe)
    hits: list[str] = []

    for sec in sections:
        if sec.name.lower() != ns.section.lower():
            continue
        buf = sec.data
        for i in range(len(buf) - 6):
            start = i
            prefixes: list[int] = []
            rex = None
            while i < len(buf):
                b = buf[i]
                if b in PREFIX_BYTES or 0x40 <= b <= 0x4F:
                    prefixes.append(b)
                    if 0x40 <= b <= 0x4F:
                        rex = b
                    i += 1
                    continue
                break
            if i >= len(buf):
                i = start
                continue

            va = image_base + sec.va + start
            op = buf[i]
            kind = None
            length = 0
            disp_off = None

            if op in (0x8D, 0x8B, 0x89) and i + 5 < len(buf):
                modrm = buf[i + 1]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                # No SIB form for RIP-relative; mod=0 rm=5.
                if mod == 0 and rm == 5:
                    kind = {0x8D: "lea", 0x8B: "mov-load", 0x89: "mov-store"}.get(op)
                    disp_off = i + 2
                    length = (i - start) + 6
            elif op in (0xE8, 0xE9) and i + 4 < len(buf):
                kind = "call-rel32" if op == 0xE8 else "jmp-rel32"
                disp_off = i + 1
                length = (i - start) + 5
            elif op == 0x0F and i + 5 < len(buf) and buf[i + 1] in range(0x80, 0x90):
                kind = "jcc-rel32"
                disp_off = i + 2
                length = (i - start) + 6

            if disp_off is None:
                i = start
                continue

            disp = struct.unpack_from("<i", buf, disp_off)[0]
            resolved = va + length + disp
            if resolved in targets:
                hits.append(
                    f"0x{va:x} target=0x{resolved:x} kind={kind} bytes={buf[start:start + length].hex()}"
                )
            i = start

    lines = [
        f"imageBase=0x{image_base:x}",
        "targets=" + ", ".join(f"0x{t:x}" for t in sorted(targets)),
        f"hits={len(hits)}",
        "",
        *hits,
    ]
    ns.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {ns.out} hits={len(hits)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
