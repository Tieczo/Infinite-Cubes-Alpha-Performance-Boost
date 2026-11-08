# Cube World Alpha

`Cube.exe` and `Server.exe` of the Cube World Alpha (2013-07-20), Windows x86..

## Requirements

- CMake 3.20 or newer
- Python 3
- One of:
  - Linux: MinGW-w64 i686 GCC (`gcc-mingw-w64-i686`, `g++-mingw-w64-i686`) and Ninja
  - Windows: Visual Studio 2022 with the C++ workload and the Windows SDK
- Internet access at the first configure, which downloads zlib and FreeImage (pinned by SHA-256)

## Build

MinGW (Linux):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
cmake --build build
```

Visual Studio 2022 (developer command prompt):

```bat
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A Win32
cmake --build build-msvc --config Release
```

Options:

| option | default | effect |
| --- | --- | --- |
| `-DCUBE_SCRIPTING=OFF` | `ON` | leave out the interface script engine (abstr::Machine and cube::System); the game then skips the script check of `db.dat` at start |
| `-DREBUILD_SHADERS=ON` / `OFF` | `ON` when fxc is found | compile the HLSL with fxc / use the checked-in shader bytecode |
| `-DFXC_EXECUTABLE=PATH` | | the fxc to use |

Output: `build/Cube.exe` and `build/Server.exe` (or `build-msvc/Release/`).

## Run

Copy the data from your own installation next to the executables:
- `data1.db` to `data4.db`, `db.dat`, `resource1.dat`, `resource2.dat`, `logo.bmp`;
- the `.plx` files.

Then create an empty `Save/` directory. `Server.exe` listens on TCP port 12345 and stops on `q`.

The server's own settings (not in the original) are lines after the seed in its `server.cfg`, one `key value` per line
in any order. A missing, unknown or invalid line keeps the original's value, so a `server.cfg` with only the seed works
as before, and the original `Server.exe` reads the same file (it only reads the seed). The server reads the file once a
second, so a change applies while it runs, and prints the values it uses.

```
123456
creatureDistance 400
guardianReliableBlock 1
```

- `creatureDistance N` (100 to 400, default 100): creatures come to life, stay and are sent to the players up to N/100
  times as far, as the Creature Distance option does in single player. Original clients can connect as before; at 400
  each one receives about 16 times as much creature data.
- `guardianReliableBlock 1` (default 0): blocked hits cost a player who is a guardian warrior no block power, as the
  Guardian Reliable Block option does in single player. Online the server decides the block.

The game runs on Windows 7 and later. Like the original, its sound uses XAudio2 2.7 from the
[DirectX End-User Runtime](https://www.microsoft.com/en-us/download/details.aspx?id=8109) (June 2010), which
Windows does not include: without it, the game reports that XAudio2 could not be initialized and exits.
