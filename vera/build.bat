@echo off
rem =====================================================================
rem  CX16-OTHELLO -> Apple II VERA build
rem
rem  1. generate the asset blob        (tools\gen_assets.py)
rem  2. convert the ZSM music to PSG   (tools\zsm2psg.py)   -> MUSIC.PSG
rem  3. convert the ZSM music to VZM   (tools\gen_music.mjs) -> MUSIC.VZM
rem  4. render the soundtrack to 8 kHz PCM (tools\gen_pcm.mjs) -> OTHELLO.PCM
rem  5. compile MAIN.BIN for VERA Slot 2 and Slot 4
rem  6. assemble the bootable ProDOS HDV with Cadius (tools\build_hdv.py)
rem =====================================================================
setlocal
cd /d "%~dp0"

where mos-apple2e-clang >nul 2>&1
if not errorlevel 1 (
    set "CC=mos-apple2e-clang"
) else if exist "C:\dev\llvm-mos-sdk\install\bin\mos-apple2e-clang.bat" (
    set "CC=C:\dev\llvm-mos-sdk\install\bin\mos-apple2e-clang.bat"
) else if exist "C:\dev\llvm-mos-sdk\bin\mos-apple2e-clang.bat" (
    set "CC=C:\dev\llvm-mos-sdk\bin\mos-apple2e-clang.bat"
) else (
    set "CC=mos-apple2e-clang"
)

where python >nul 2>&1
if not errorlevel 1 (
    set "PY=python"
) else (
    set "PY=py -3"
)

if not exist build mkdir build
if not exist generated mkdir generated

echo [1/7] Generating assets (tools\gen_assets.py) ...
call "%PY%" tools\gen_assets.py
if errorlevel 1 goto fail

echo [2/7] Converting ZSM music to VERA PSG (tools\zsm2psg.py) ...
call "%PY%" tools\zsm2psg.py
if errorlevel 1 goto fail

echo [3/7] Converting ZSM music to VERA PSG+FM (tools\gen_music.mjs) ...
call node tools\gen_music.mjs
if errorlevel 1 goto fail

echo [4/7] Rendering the soundtrack to 8 kHz PCM (tools\gen_pcm.mjs) ...
call node tools\gen_pcm.mjs
if errorlevel 1 goto fail

echo [5/7] Compiling Applesoft STARTUP (tools\gen_startup.mjs) ...
call node tools\gen_startup.mjs
if errorlevel 1 goto fail

echo [6/7] Compiling (Slot 2 + Slot 4 images) ...
call "%CC%" -Os -mcpu=mos65c02 -Isrc -Igenerated -T src\link1000.ld -Wl,-Map=build\main.map -o build\main.bin src\main.c src\video.c src\game.c src\menu.c src\input.c src\audio.c src\disk.c src\sfx_data.c src\mli.s src\mouse.s src\pcmfeed.s
if errorlevel 1 goto fail
call "%CC%" -Os -mcpu=mos65c02 -Isrc -Igenerated -DVERA_BASE=0xC400 -T src\link1000.ld -o build\main4.bin src\main.c src\video.c src\game.c src\menu.c src\input.c src\audio.c src\disk.c src\sfx_data.c src\mli.s src\mouse.s src\pcmfeed.s
if errorlevel 1 goto fail

echo [7/7] Building ProDOS HDV with Cadius (tools\build_hdv.py) ...
call "%PY%" tools\build_hdv.py %*
if errorlevel 1 goto fail

echo.
echo OK: cx16-othello.hdv built.
endlocal & exit /b 0

:fail
echo.
echo BUILD FAILED
endlocal & exit /b 1



