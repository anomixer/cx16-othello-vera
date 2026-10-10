"""Package the game and the official, checksum-pinned X16 WASM release."""

from hashlib import sha256
from pathlib import Path
from urllib.error import URLError
from urllib.request import urlopen
from zipfile import ZipFile, ZIP_DEFLATED
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = "x16emu_wasm-r49.zip"
URL = f"https://github.com/X16Community/x16-emulator/releases/download/r49/{ARCHIVE}"
DIGEST = "9711036cf5b33504815b278fa872b8ec20aef9cb609fd7b17f3685d91312628f"
RESOURCES = ("OTHELLO.PRG", "TILES.DAT", "FONTMAP.DAT", "HELP.DAT", "OTHELLO.ZSM", "TILE.ZSM")


def main():
    cache = ROOT / "build/site-cache"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / ARCHIVE
    if not archive.exists():
        print("Downloading official Commander X16 WebAssembly release r49…", flush=True)
        try:
            with urlopen(URL, timeout=60) as response:
                data = response.read()
        except (URLError, TimeoutError) as error:
            raise SystemExit(f"Cannot download {URL}: {error}\n"
                             f"For an offline build, place the release archive at {archive}.")
        if sha256(data).hexdigest() != DIGEST:
            raise SystemExit("Downloaded emulator archive checksum mismatch.")
        archive.write_bytes(data)
    if sha256(archive.read_bytes()).hexdigest() != DIGEST:
        raise SystemExit(f"Emulator archive checksum mismatch. Remove {archive} and retry.")

    # Check inputs before replacing the previous build.
    for name in RESOURCES:
        if not (ROOT / "src" / name).is_file():
            raise SystemExit(f"Missing game resource: {name}. Run make site from the repository root.")
    output = ROOT / "build/site"
    shutil.rmtree(output, ignore_errors=True)
    shutil.copytree(ROOT / "site", output)
    (output / "emulator").mkdir()
    with ZipFile(archive) as package:
        for name in ("x16emu.js", "x16emu.wasm", "x16emu.data"):
            (output / "emulator" / name).write_bytes(package.read(name))
    (output / "game").mkdir()
    for name in RESOURCES:
        shutil.copy2(ROOT / "src" / name, output / "game" / name)
    (output / "game/manifest.json").write_text(
        json.dumps({"resources": RESOURCES}, indent=2) + "\n", encoding="utf-8")
    with ZipFile(output / "CX16-OTHELLO.ZIP", "w", compression=ZIP_DEFLATED) as package:
        for name in RESOURCES:
            package.write(output / "game" / name, name)
        package.write(ROOT / "assets/README.md", "ASSET-NOTICES.txt")
        package.write(ROOT / "LICENSE", "CODE-LICENSE.txt")
    shutil.copy2(ROOT / "assets/README.md", output / "asset-notices.txt")
    shutil.copy2(ROOT / "img/cx16-othello.gif", output / "gameplay.gif")
    shutil.copy2(ROOT / "LICENSE", output / "game-license.txt")
    (output / ".nojekyll").touch()
    print(f"Site ready: {output}\nPreview: make serve → http://localhost:8000")


if __name__ == "__main__":
    main()
