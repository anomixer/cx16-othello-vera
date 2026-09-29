#!/usr/bin/env python3
"""Convert a Commander X16 .ZSM stream into the compact VERA-PSG stream used by
the Apple II port.

ZSM command stream (data starts at byte 16):
  0x00..0x3F  PSG write: low 6 bits = VERA PSG register index (voice*4 + 0..3),
              followed by one value byte.  The X16 PSG *is* the VERA PSG, so
              these bytes are copied verbatim.
  0x40        EXTCMD: next byte low 6 bits = payload length (skipped).
  0x41..0x7F  YM2151 FM write batch: low 6 bits = number of (reg,value) pairs.
  0x80        End of stream.
  0x81..0xFF  Delay of (cmd & 0x7F) ticks (1 tick = 1/60 s).

The Apple II VERA card has no YM2151, so the FM part is re-voiced onto the
free VERA PSG channels (voices 0..7) from the key-code / key-on / TL data.

Output stream format (consumed by src/audio.c at 60 Hz):
  0x80|reg  followed by value   -> write VERA PSG register `reg`
  0x00..0xFE                    -> wait this many frames
  0xFF                          -> end of stream (loop to the loop offset)
"""
from __future__ import annotations
import sys
from pathlib import Path

PSG_RATE = 25_000_000 / 512          # VERA internal audio clock, 48828.125 Hz

# YM2151 key code -> semitone offset from C (OPM key codes).
NOTE_MAP = {14: 0, 0: 1, 1: 2, 2: 3, 4: 4, 5: 5, 6: 6, 8: 7, 9: 8, 10: 9, 12: 10, 13: 11}

# ZSMKit's YM2151 pseudo-register convention (as used by every X16 ZSM in these
# repositories):
#   $08        key on/off   bits 2:0 = channel, bits 7:3 = operator mask
#   $20..$27   KC  (octave 6:4, key code 3:0) per channel
#   $28..$2F   TL  (output level, 0 = loudest) per channel
#   $30..$37   FSYL (fine pitch) per channel - ignored
#
# CX16-OTHELLO's native PSG part occupies voices 4..15, so the re-voiced FM
# channels use the free voices 0..3.  Voice 3 also carries the "thumb" click,
# which is the least active FM channel (ch5, 96 notes) to keep collisions rare.
FM_VOICE = {3: 0, 4: 1, 5: 3, 6: 2, 0: 1, 1: 1, 2: 1, 7: 2}
FM_PSG_BASE = {ch: v * 4 for ch, v in FM_VOICE.items()}


def midi_to_freq(midi: int) -> int:
    f = 440.0 * 2 ** ((midi - 69) / 12)
    return max(0, min(0x3FFF, round(f * 131072 / PSG_RATE)))


def convert(src: Path, *, fm_channels=None, fm_wave=0x3F, fm_vol_scale=0.62,
            fm_tail=(46, 40, 33, 25, 16, 8, 0), reg_map=None, verbose=True):
    raw = src.read_bytes()
    loop_offset = int.from_bytes(raw[3:6], "little")
    loop_enabled = raw[12] != 0

    events: dict[int, list[tuple[int, int]]] = {}

    def add(tick: int, reg: int, val: int):
        reg &= 0x3F
        if reg_map is not None:
            reg = reg_map.get(reg, reg)
        events.setdefault(tick, []).append((reg, val & 0xFF))

    ptr, tick = 16, 0
    kc = [0] * 8
    tl = [127] * 8
    keyon = [0] * 8
    fm_used: dict[int, int] = {}
    loop_tick = -1
    loop_target = loop_offset if (loop_enabled and loop_offset > 16) else 16

    while ptr < len(raw):
        if loop_tick < 0 and ptr >= loop_target:
            loop_tick = tick
        cmd = raw[ptr]
        ptr += 1
        if cmd < 0x40:
            if ptr >= len(raw):
                break
            add(tick, cmd, raw[ptr])
            ptr += 1
        elif cmd == 0x40:
            if ptr >= len(raw):
                break
            ext = raw[ptr]
            ptr += 1 + (ext & 0x3F)
        elif cmd < 0x80:
            for _ in range(cmd & 0x3F):
                if ptr + 1 >= len(raw):
                    break
                reg, val = raw[ptr], raw[ptr + 1]
                ptr += 2
                if 0x28 <= reg <= 0x2F:
                    tl[reg - 0x28] = val & 0x7F
                elif 0x20 <= reg <= 0x27:
                    kc[reg - 0x20] = val & 0x7F
                elif reg == 0x08:
                    ch = val & 0x07
                    on = bool((val >> 3) & 0x0F)
                    if keyon[ch] != on:
                        keyon[ch] = on
                        if not on:
                            add(tick, FM_PSG_BASE[ch] + 2, 0x00)
                            continue
                        if fm_channels is not None and ch not in fm_channels:
                            continue
                        octv = (kc[ch] >> 4) & 0x07
                        semi = NOTE_MAP.get(kc[ch] & 0x0F, 0)
                        midi = (octv + 1) * 12 + semi
                        f = midi_to_freq(midi)
                        base = FM_PSG_BASE[ch]
                        add(tick, base + 0, f & 0xFF)
                        add(tick, base + 1, (f >> 8) & 0x3F)
                        add(tick, base + 3, fm_wave)
                        vol = int(round((127 - tl[ch]) * 63 / 127 * fm_vol_scale))
                        vol = max(0, min(60, vol))
                        add(tick, base + 2, 0xC0 | vol)
                        # PSG has no ADSR: emulate the plucked decay envelope.
                        for d, v in enumerate(fm_tail[1:], start=1):
                            add(tick + d * 2, base + 2, 0xC0 | min(v, vol))
                        fm_used[ch] = fm_used.get(ch, 0) + 1
        elif cmd == 0x80:
            break
        else:
            tick += cmd & 0x7F

    total_ticks = tick

    # ---- serialise with change detection + 2-byte record block alignment ----
    out = bytearray()
    last: dict[int, int] = {}
    pending_delay = 0
    loop_pos = -1

    def emit(byte: int):
        nonlocal loop_pos
        if loop_pos < 0 and tick_index == loop_tick:
            loop_pos = len(out)
        out.append(byte)

    for tick_index in range(total_ticks + 1):
        if tick_index == total_ticks:
            break
        evs = events.get(tick_index)
        if not evs:
            pending_delay += 1
            continue
        # Flush accumulated idle frames before this tick's writes.
        while pending_delay > 0:
            chunk = min(pending_delay, 126)
            pending_delay -= chunk
            emit(chunk)
        for reg, val in evs:
            if last.get(reg) == val:
                continue
            last[reg] = val
            if (len(out) & 0x1FF) == 0x1FF:
                out.append(0)          # keep the 2-byte record inside one block
            emit(0x80 | reg)
            emit(val)
        pending_delay += 1

    if loop_pos < 0:
        loop_pos = 0
    out.append(0xFF)

    if verbose:
        print(f"  {src.name}: {len(raw)} -> {len(out)} bytes "
              f"({total_ticks} ticks = {total_ticks/60:.1f}s, loop@{loop_pos}, "
              f"FM voices={sorted(fm_used.items())})")
    return bytes(out), loop_pos, total_ticks


def main(argv):
    root = Path(__file__).resolve().parent.parent
    gen = root / "generated"
    gen.mkdir(exist_ok=True)

    # Background music: "Corridors of Time" (GTR3QQ), 12 native PSG voices
    # (voices 4..15) plus the four active YM2151 channels re-voiced onto the
    # free voices 0..3.
    data, loop, ticks = convert(root / "assets" / "othello.zsm",
                                fm_channels={3, 4, 5, 6})
    (gen / "music.psg").write_bytes(data)
    (gen / "music_table.h").write_text(
        "/* Auto-generated by tools/zsm2psg.py - do not edit. */\n"
        "#ifndef _MUSIC_TABLE_H\n#define _MUSIC_TABLE_H\n"
        f"#define MUSIC_SIZE       {len(data)}u\n"
        f"#define MUSIC_LOOP       {loop}u\n"
        f"#define MUSIC_TICKS      {ticks}u\n"
        "#endif\n", encoding="utf-8")

    # Stone-placement "thumb" click: native PSG voice 0.  Voice 0 is taken by a
    # re-voiced FM channel, so shift the click onto voice 3 (the least active).
    thumb_map = {r: 12 + (r & 3) for r in range(4)}
    data, loop, ticks = convert(root / "assets" / "tile.zsm",
                                fm_channels=set(), reg_map=thumb_map)
    (gen / "sfx_thumb.psg").write_bytes(data)
    (gen / "sfx_table.h").write_text(
        "/* Auto-generated by tools/zsm2psg.py - do not edit. */\n"
        "#ifndef _SFX_TABLE_H\n#define _SFX_TABLE_H\n"
        f"#define SFX_THUMB_SIZE   {len(data)}u\n"
        f"#define SFX_THUMB_TICKS  {ticks}u\n"
        "#endif\n", encoding="utf-8")

    # The click is short enough to live in the binary itself.
    rows = []
    for i in range(0, len(data), 16):
        rows.append("    " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",")
    (root / "src" / "sfx_data.c").write_text(
        "/* Auto-generated by tools/zsm2psg.py - do not edit. */\n"
        "#include <stdint.h>\n"
        "const uint8_t sfx_thumb[] = {\n" + "\n".join(rows) + "\n};\n",
        encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
