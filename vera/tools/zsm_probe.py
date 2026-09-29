#!/usr/bin/env python3
"""Probe a Commander X16 .ZSM file: report PSG vs YM2151-FM command mix.

ZSM command stream (data begins at byte offset 16):
  0x00..0x3F : PSG write. Low 6 bits = VERA PSG register index (0..63),
               followed by one value byte.
  0x40       : EXTCMD. Next byte: low 6 bits = payload length, bits 6:7 = channel.
  0x41..0x7F : YM2151 FM write batch. Low 6 bits = number of (reg,value) pairs.
  0x80       : End of stream.
  0x81..0xFF : Delay of (cmd & 0x7F) ticks.
"""
import sys
from collections import Counter


def parse(path):
    d = open(path, 'rb').read()
    rate = int.from_bytes(d[4:8], 'little')
    loop = int.from_bytes(d[8:12], 'little')
    loopen = d[12]
    ptr = 16
    psg = Counter()
    fm = Counter()
    ext = Counter()
    delays = []
    ticks = 0
    ncmd = 0
    while ptr < len(d):
        c = d[ptr]
        ptr += 1
        ncmd += 1
        if c < 0x40:
            if ptr >= len(d):
                print("TRUNCATED psg at", ptr - 1)
                break
            psg[c] += 1
            ptr += 1
        elif c == 0x40:
            if ptr >= len(d):
                break
            e = d[ptr]
            ptr += 1
            ext[e & 0xC0] += 1
            ptr += e & 0x3F
        elif c < 0x80:
            n = c & 0x3F
            fm[n] += 1
            ptr += n * 2
        elif c == 0x80:
            break
        else:
            delays.append(c & 0x7F)
            ticks += c & 0x7F
    print(f"=== {path} ===")
    print(f"  size={len(d)} rate={rate} loop_pos={loop} loop_en={loopen}")
    print(f"  commands consumed={ncmd} end ptr={ptr} (of {len(d)})")
    print(f"  total delay ticks={ticks}  ({ticks / max(rate,1):.2f}s at {rate}Hz)")
    print(f"  PSG writes={sum(psg.values())}  FM batches={sum(fm.values())}  ext={sum(ext.values())}")
    voices = Counter()
    for reg, n in psg.items():
        voices[reg >> 2] += n
    print("  PSG writes per voice:", dict(sorted(voices.items())))
    print("  PSG reg histogram:", dict(sorted(psg.items())))
    print("  ext channels:", dict(ext))
    return d


for p in sys.argv[1:]:
    parse(p)
