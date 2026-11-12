---
name: wine-build-test
description: Build the Windows Cube.exe/Server.exe of Infinite Cubes with MinGW (32 and 64 bit) and test them under Wine without touching anyone's playing setup. Use when a change must be checked on Windows, for releases, and for anything in Win32-only code paths.
---

# Windows build (MinGW) and Wine test

Build (separate build folders per target, see AGENTS.md "Building"):

    cmake -S alpha -B alpha/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
    cmake -S alpha -B alpha/build64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake -DCUBE_SCRIPTING=OFF
    cmake --build alpha/build && cmake --build alpha/build64

- Never reuse the Linux build's FreeImage source (`_deps`) for MinGW: the WIN32 configure patches it in place and breaks
  every Linux build. Let MinGW fetch its own or give it a private unpacked copy.
- Shared game code must build on all three (Linux, MinGW 32, MinGW 64); `int` vs pointer size matters on 64 bit
  (the script engine stays off there).

Test under Wine:

- **Never in the user's Wine prefix or game folder.** Make a copy of the prefix (`cp -a`) or a fresh one (`wineboot -i`)
  and a test folder with copies of the game's data, then put the new `Cube.exe`/`Server.exe` there.
- **Never `wineserver -k` without `WINEPREFIX` set to your copy**: it would kill the user's running game. Prefer ending
  your own processes by PID.
- Run invisible: `WINEPREFIX=<copy> gamescope --backend headless -W 2560 -H 1440 -w 2560 -h 1440 -- wine Cube.exe`;
  input via `tools/multiplayer/xt.py` (XTest on gamescope's Xwayland, display from "Starting Xwayland on :N" in its
  output) or `hand.exe` (built by `tools/multiplayer/setup.sh`); screenshots with `gamescopectl screenshot`.
- Check: start, character, world loads, attacks, the options page "Performance" with all switches, an option toggled
  and kept in `options-InfiniteCubes.cfg` (the original's `options.cfg` unchanged), 64-bit Cube.exe joins the original
  Server.exe (skill `multiplayer-test`).
- fps under Wine are rough (one run, the fps display); real measurements are Linux native (skill `measure-performance`).
- A rare crash at the title screen (~1 in 30 starts, DXVK) is known; a crash after start counts.
