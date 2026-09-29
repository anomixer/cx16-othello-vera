@echo off
rem =====================================================================
rem  CX16-OTHELLO -> Apple II VERA build
rem
rem  1. generate the asset blob  (tools\gen_assets.py)
rem  2. convert the ZSM music    (tools\zsm2psg.py)
rem  3. compile MAIN.BIN for VERA Slot 2 and Slot 4
rem  4. assemble the bootable ProDOS HDV (tools\build_hdv.py)
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

echo [1/5] Generating assets (tools\gen_assets.py) ...
call "%PY%" tools\gen_assets.py
if errorlevel 1 goto fail

echo [2/5] Converting ZSM music to VERA PSG (tools\zsm2psg.py) ...
call "%PY%" tools\zsm2psg.py
if errorlevel 1 goto fail

echo [3/5] Compiling Applesoft STARTUP (tools\gen_startup.mjs) ...
call node tools\gen_startup.mjs
if errorlevel 1 goto fail

echo [4/5] Compiling (Slot 2 + Slot 4 images) ...
call "%CC%" -Os -mcpu=mos65c02 -Isrc -Igenerated -T src\link1000.ld -Wl,-Map=build\main.map -o build\main.bin src\main.c src\video.c src\game.c src\menu.c src\input.c src\audio.c src\disk.c src\sfx_data.c src\mli.s src\mouse.s
if errorlevel 1 goto fail
call "%CC%" -Os -mcpu=mos65c02 -Isrc -Igenerated -DVERA_BASE=0xC400 -T src\link1000.ld -o build\main4.bin src\main.c src\video.c src\game.c src\menu.c src\input.c src\audio.c src\disk.c src\sfx_data.c src\mli.s src\mouse.s
if errorlevel 1 goto fail

echo [5/5] Building ProDOS HDV (tools\build_hdv.py) ...
call "%PY%" tools\build_hdv.py %*
if errorlevel 1 goto fail

echo.
echo OK: cx16-othello.hdv built.
endlocal & exit /b 0

:fail
echo.
echo BUILD FAILED
endlocal & exit /b 1
