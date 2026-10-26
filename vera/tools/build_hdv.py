#!/usr/bin/env python3
"""Create the bootable CX16-OTHELLO HDV with Cadius.

Cadius owns the ProDOS volume, catalog, block allocation and file timestamps.
Generated files are staged with Cadius' ``#TTAAAA`` suffix, where TT is the
ProDOS file type and AAAA is the auxiliary type.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


VOLUME_NAME = "CX16.OTHELLO"


def cadius_add(cadius: Path, image: Path, source: Path) -> None:
    subprocess.run(
        [str(cadius), "ADDFILE", str(image), f"/{VOLUME_NAME}", str(source)],
        check=True,
    )


def main(output: Path | None = None, cadius_arg: Path | None = None) -> int:
    here = Path(__file__).resolve().parent
    vera = here.parent
    build = vera / "build"
    generated = vera / "generated"
    system_dir = vera / "assets" / "prodos-system"
    cadius = (cadius_arg or here / "cadius.exe").resolve()
    out = (output or vera / "cx16-othello.hdv").resolve()

    for path in (cadius, build / "STARTUP", build / "main.bin", build / "main4.bin",
                 generated / "assets.bin", generated / "music.psg",
                 generated / "music.vzm"):
        if not path.is_file():
            raise FileNotFoundError(f"Required build input not found: {path}")
    for name in ("PRODOS", "BASIC.SYSTEM", "CLOCK.SYSTEM"):
        if not (system_dir / f"{name}#FF0000").is_file():
            raise FileNotFoundError(f"ProDOS system file not found: {system_dir / name}")

    stage = build / "cadius-files"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    # Cadius infers ProDOS file type and aux type from this filename suffix.
    staged: list[Path] = []

    def stage_bytes(name: str, suffix: str, data: bytes) -> Path:
        path = stage / f"{name}#{suffix}"
        path.write_bytes(data)
        staged.append(path)
        return path

    stage_bytes("OTHELLO", "062000", b"OTHELLO")
    stage_bytes("STARTUP", "FC0801", (build / "STARTUP").read_bytes())
    for prodos_name, build_name in (("MAIN.BIN", "main.bin"), ("MAIN4.BIN", "main4.bin")):
        data = (build / build_name).read_bytes()
        if len(data) < 4:
            raise ValueError(f"Invalid Apple II BIN file: {build / build_name}")
        load_address = int.from_bytes(data[:2], "little")
        body_length = int.from_bytes(data[2:4], "little")
        body = data[4:]
        if load_address != 0x1400 or body_length != len(body):
            raise ValueError(f"Invalid Apple II BIN header in {build / build_name}")
        stage_bytes(prodos_name, f"06{load_address:04X}", body)
    stage_bytes("ASSETS.BIN", "060000", (generated / "assets.bin").read_bytes())
    stage_bytes("MUSIC.PSG", "060000", (generated / "music.psg").read_bytes())
    stage_bytes("MUSIC.VZM", "060000", (generated / "music.vzm").read_bytes())

    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    subprocess.run([str(cadius), "CREATEVOLUME", str(out), VOLUME_NAME, "800KB"], check=True)
    for name in ("PRODOS", "BASIC.SYSTEM", "CLOCK.SYSTEM"):
        cadius_add(cadius, out, system_dir / f"{name}#FF0000")
    for source in staged:
        cadius_add(cadius, out, source)

    subprocess.run([str(cadius), "CHECKVOLUME", str(out)], check=True)
    subprocess.run([str(cadius), "CATALOG", str(out)], check=True)
    print(f"Built {out} ({out.stat().st_size // 1024} KB) with Cadius")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", type=Path, help="Optional output HDV path")
    parser.add_argument("--cadius", type=Path, help="Path to Cadius executable")
    args = parser.parse_args()
    raise SystemExit(main(args.output, args.cadius))
