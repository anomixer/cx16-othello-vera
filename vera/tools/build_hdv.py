#!/usr/bin/env python3
"""Assemble the bootable 800 KB ProDOS HDV for the CX16-OTHELLO Apple II port.

Boot chain:  ProDOS -> BASIC.SYSTEM -> Applesoft STARTUP -> BRUN MAIN.BIN
STARTUP probes the VERA card slot and picks MAIN.BIN (slot 2) or MAIN4.BIN
(slot 4).

Layout (block numbers, 512 bytes each):
    0-6     ProDOS master/volume header, directory, storage bitmap
    7-99    reserved (PRODOS / CLOCK.SYSTEM / BASIC.SYSTEM from the base image)
   100-...  STARTUP, MAIN.BIN, MAIN4.BIN
   900-...  assets.bin  (raw blocks, read by MLI - no directory entry)
  1000-...  music.psg   (raw blocks, read by MLI - no directory entry)

The raw ranges are marked used in the storage bitmap so ProDOS never hands
them to a named file.  Mirrors src/disk.h - keep the two in sync.
"""
from __future__ import annotations
import sys
from pathlib import Path

BLOCK = 512
RESERVE = 100
KEEP = {"PRODOS", "CLOCK.SYSTEM", "BASIC.SYSTEM"}

ASSET_START_BLOCK = 900     # must match ASSET_START_BLOCK in src/disk.h
MUSIC_START_BLOCK = 1000    # must match MUSIC_START_BLOCK in src/disk.h


class Alloc:
    def __init__(self):
        self.used = set(range(RESERVE))
        self.new = []
        self.next = RESERVE

    def reserve(self, start: int, count: int):
        self.used.update(range(start, start + count))

    def one(self):
        while self.next in self.used:
            self.next += 1
        b = self.next
        self.used.add(b)
        self.new.append(b)
        self.next += 1
        return b


def raw_bin(path: Path, expected: int = 0x1400):
    data = path.read_bytes()
    if len(data) < 4 or (data[0] | data[1] << 8) != expected:
        raise RuntimeError(f"bad BIN header in {path}")
    return data[4:], data[0] | data[1] << 8


def put(disk: bytearray, alloc: Alloc, name: str, typ: int, aux: int, data: bytes):
    n = (len(data) + BLOCK - 1) // BLOCK
    if n == 1:
        key = alloc.one()
        disk[key * BLOCK:key * BLOCK + len(data)] = data
        return dict(name=name, storage=1, typ=typ, key=key, total=1,
                    eof=len(data), aux=aux, first=key, last=key)
    if n > 256:
        raise ValueError(f"{name}: {n} blocks exceeds a single sapling index")
    key = alloc.one()
    index = bytearray(BLOCK)
    first = 0
    for i in range(n):
        b = alloc.one()
        if i == 0:
            first = b
        index[i] = b & 0xFF
        index[256 + i] = b >> 8
        chunk = data[i * BLOCK:(i + 1) * BLOCK]
        disk[b * BLOCK:b * BLOCK + len(chunk)] = chunk
    disk[key * BLOCK:(key + 1) * BLOCK] = index
    return dict(name=name, storage=2, typ=typ, key=key, total=n + 1,
                eof=len(data), aux=aux, first=first, last=first + n - 1)


def write_raw(disk: bytearray, start_block: int, data: bytes) -> int:
    n = (len(data) + BLOCK - 1) // BLOCK
    disk[start_block * BLOCK:start_block * BLOCK + len(data)] = data
    return n


def entry(vol, offset: int, f: dict):
    vol[offset] = (f["storage"] << 4) | len(f["name"])
    vol[offset + 1:offset + 1 + len(f["name"])] = f["name"].encode("ascii")
    vol[offset + 0x10] = f["typ"]
    vol[offset + 0x11] = f["key"] & 0xFF
    vol[offset + 0x12] = f["key"] >> 8
    vol[offset + 0x13] = f["total"] & 0xFF
    vol[offset + 0x14] = f["total"] >> 8
    vol[offset + 0x15] = f["eof"] & 0xFF
    vol[offset + 0x16] = (f["eof"] >> 8) & 0xFF
    vol[offset + 0x17] = (f["eof"] >> 16) & 0xFF
    vol[offset + 0x1E] = 0xC3
    vol[offset + 0x1F] = f["aux"] & 0xFF
    vol[offset + 0x20] = f["aux"] >> 8
    vol[offset + 0x25] = 2


def main(output=None):
    here = Path(__file__).resolve().parent
    vera = here.parent
    build = vera / "build"
    gen = vera / "generated"
    out = Path(output).resolve() if output else vera / "cx16-othello.hdv"

    disk = bytearray((vera / "assets" / "800kb.hdv").read_bytes())
    total_blocks = len(disk) // BLOCK
    alloc = Alloc()

    assets = (gen / "assets.bin").read_bytes()
    music = (gen / "music.psg").read_bytes()

    n_assets = write_raw(disk, ASSET_START_BLOCK, assets)
    n_music = write_raw(disk, MUSIC_START_BLOCK, music)
    alloc.reserve(ASSET_START_BLOCK, n_assets)
    alloc.reserve(MUSIC_START_BLOCK, n_music)

    files = [put(disk, alloc, "STARTUP", 0xFC, 0x0801, (build / "STARTUP").read_bytes())]
    for name, fname in (("MAIN.BIN", "main.bin"), ("MAIN4.BIN", "main4.bin")):
        data, address = raw_bin(build / fname)
        files.append(put(disk, alloc, name, 0x06, address, data))

    # ---- rebuild the directory: keep the system files, add ours ----
    vol = memoryview(disk)[2 * BLOCK:3 * BLOCK]
    retained = []
    for i in range(1, 13):
        off = 4 + i * 39
        if not vol[off]:
            continue
        n = vol[off] & 15
        name = bytes(vol[off + 1:off + 1 + n]).decode("ascii", "replace")
        if name in KEEP:
            retained.append(bytes(vol[off:off + 39]))
    for i in range(1, 13):
        vol[4 + i * 39:4 + (i + 1) * 39] = b"\0" * 39
    for i, old in enumerate(retained, 1):
        vol[4 + i * 39:4 + i * 39 + 39] = old

    all_entries = retained + [None] * len(files)
    for i, f in enumerate(files):
        all_entries[len(retained) + i] = f
    for i, item in enumerate(all_entries[:12], 1):
        if isinstance(item, bytes):
            vol[4 + i * 39:4 + (i + 1) * 39] = item
        else:
            entry(vol, 4 + i * 39, item)

    previous = 2
    for start in range(12, len(all_entries), 13):
        db = alloc.one()
        disk[previous * BLOCK + 2:previous * BLOCK + 4] = db.to_bytes(2, "little")
        disk[db * BLOCK:db * BLOCK + BLOCK] = b"\0" * BLOCK
        disk[db * BLOCK:db * BLOCK + 2] = previous.to_bytes(2, "little")
        page = memoryview(disk)[db * BLOCK:(db + 1) * BLOCK]
        for i, item in enumerate(all_entries[start:start + 13]):
            if isinstance(item, bytes):
                page[4 + i * 39:4 + (i + 1) * 39] = item
            else:
                entry(page, 4 + i * 39, item)
        previous = db

    vol[0x25] = (len(retained) + len(files)) & 0xFF
    vol[0x26] = (len(retained) + len(files)) >> 8

    # A set bit means FREE; clear the bits of every block we now own.
    for b in alloc.used:
        disk[6 * BLOCK + b // 8] &= (0xFF ^ (1 << (7 - b % 8))) & 0xFF

    if max(alloc.used) >= total_blocks:
        raise ValueError("HDV capacity exceeded")

    out.write_bytes(disk)
    print(f"  {out.name}: {len(disk) // 1024} KB, "
          f"{len(retained) + len(files)} files, "
          f"assets {n_assets} blocks @{ASSET_START_BLOCK}, "
          f"music {n_music} blocks @{MUSIC_START_BLOCK}")
    print("  files: " + ", ".join(f["name"] for f in files))
    # Report each file's data-block range so the documented layout stays honest.
    for f in files:
        print(f"  {f['name']:10s} blocks {f['first']}-{f['last']} "
              f"({f['total']} incl. index), {f['eof']} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else None))
