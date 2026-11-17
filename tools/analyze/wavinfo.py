"""Print every chunk and header field of a WAV file, and check it against the RIFF/WAVE rules.

    python tools/analyze/wavinfo.py renders/foo.wav [more.wav ...]

Written straight from the spec, independently of the C++ WavWriter/readWav, so it can catch bugs the
writer and reader would agree on. Exit code 1 if any file breaks a rule.

Checks: RIFF size == file size − 8; chunks tile the file exactly (with pad bytes after odd sizes);
fmt is consistent (block align = channels·bits/8, byte rate = rate·block align); non-PCM formats have
cbSize and a fact chunk whose frame count matches the data; data is a whole number of frames.
"""
from __future__ import annotations

import struct
import sys

FORMATS = {1: "PCM", 3: "IEEE float", 0xFFFE: "EXTENSIBLE"}


def inspect(path: str) -> list[str]:
    problems: list[str] = []
    b = open(path, "rb").read()
    print(f"\n{path}: {len(b)} bytes")
    if len(b) < 12 or b[0:4] != b"RIFF" or b[8:12] != b"WAVE":
        return ["not RIFF/WAVE"]
    riff_size = struct.unpack_from("<I", b, 4)[0]
    print(f"  RIFF size {riff_size}  (file - 8 = {len(b) - 8})")
    if riff_size != len(b) - 8:
        problems.append("RIFF size != file size - 8")

    fmt = None
    fact_frames = None
    data_size = None
    at = 12
    while at + 8 <= len(b):
        cid, size = b[at:at + 4], struct.unpack_from("<I", b, at + 4)[0]
        body = at + 8
        print(f"  @{at:<6} chunk {cid.decode('latin-1')!r:8} size {size}")
        if cid == b"fmt ":
            tag, ch, rate, byte_rate, align, bits = struct.unpack_from("<HHIIHH", b, body)
            fmt = (tag, ch, rate, byte_rate, align, bits)
            print(f"           format {tag} ({FORMATS.get(tag, '?')}), {ch} ch, {rate} Hz, "
                  f"{bits} bit, block align {align}, byte rate {byte_rate}")
            if align != ch * bits // 8:
                problems.append("block align != channels x bits/8")
            if byte_rate != rate * align:
                problems.append("byte rate != rate x block align")
            if tag != 1:
                if size < 18:
                    problems.append("non-PCM fmt chunk lacks cbSize (size < 18)")
                else:
                    print(f"           cbSize {struct.unpack_from('<H', b, body + 16)[0]}")
        elif cid == b"fact":
            fact_frames = struct.unpack_from("<I", b, body)[0]
            print(f"           frames {fact_frames}")
        elif cid == b"data":
            data_size = size
        at = body + size + (size & 1)
    if at != len(b):
        problems.append(f"chunks end at {at}, file ends at {len(b)}")

    if fmt is None or data_size is None:
        problems.append("missing fmt or data chunk")
    else:
        tag, ch, rate, _, align, _ = fmt
        frames, rem = divmod(data_size, align)
        print(f"  data: {frames} frames = {frames / rate:.4f} s")
        if rem:
            problems.append("data size is not a whole number of frames")
        if tag != 1 and fact_frames is None:
            problems.append("non-PCM file has no fact chunk")
        if fact_frames is not None and fact_frames != frames:
            problems.append(f"fact frames {fact_frames} != data frames {frames}")

    print("  OK" if not problems else "  PROBLEMS: " + "; ".join(problems))
    return problems


def main() -> None:
    bad = sum(bool(inspect(p)) for p in sys.argv[1:])
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
