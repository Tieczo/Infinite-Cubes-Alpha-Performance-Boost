# Native Linux build

The alpha builds as a native 64-bit Linux program with GCC or Clang. The game code stays as it is;
this folder supplies the Windows parts it uses:

| Windows                         | Linux                                                   |
|---------------------------------|---------------------------------------------------------|
| Direct3D 9                      | DXVK-native (`libdxvk_d3d9.so`, Direct3D 9 on Vulkan)   |
| window, messages, DirectInput 8 | SDL3 (`main.cpp`)                                       |
| XAudio2 2.7                     | FAudio (`XAudio2_7.h`)                                  |
| critical sections, threads, timers | `windows.h`, `Thread.cpp` (C++ standard library)     |
| Winsock                         | POSIX sockets (`winsock2.h`)                            |
| glu32 tessellator               | Mesa libGLU                                             |

`windows.h` here extends DXVK-native's `windows.h` (found next in the include path), which declares the
basic types and COM. The shaders are the checked-in bytecode headers (fxc is not needed).

## Building

DXVK-native and FAudio are not Arch packages in the needed form; build them once from source
(meson, ninja, cmake, glslang, sdl3), then:

    cd alpha
    cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCUBE_SCRIPTING=OFF \
      -DDXVK_NATIVE_DIR=<dxvk-native prefix> -DFAUDIO_DIR=<FAudio prefix>
    cmake --build build-linux

Without `FAUDIO_DIR` the system's FAudio is used (pkg-config). The script engine stays off, as in
the x64 Windows build (it keeps pointers in 32-bit ints).

`Cube` looks for `lib/libdxvk_d3d9.so` (and FAudio) next to itself (`$ORIGIN/lib`) and changes into
its own folder at start, so the folder holds the executable, `lib/` and the game's data files
(data1-4.db, db.dat, resource1/2.dat, logo.bmp, *.plx, options.cfg). `Server` needs no libraries.
