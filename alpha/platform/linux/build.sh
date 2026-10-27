#!/bin/bash
# Builds the native Linux alpha and everything it needs, without root:
#   1. DXVK-native (Direct3D 9 on Vulkan, SDL3 window system) and FAudio (XAudio2) from source,
#      into build-linux/deps (once; later runs reuse them)
#   2. Cube and Server (CMake, build-linux)
#   3. given your own Cube World Alpha folder: a playable folder (default build-linux/game) with the
#      programs, lib/ and copies of the game's data files. Running it again updates the programs
#      and keeps Save/ and options.cfg.
#
# Usage:  alpha/platform/linux/build.sh [<your Cube World Alpha folder> [<target folder>]]
# Needs:  git cmake ninja meson gcc glslang sdl3 glu python3 (Arch: base-devel git cmake ninja
#         meson glslang sdl3 glu python) and a Vulkan driver.
set -e

ALPHA="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"
BUILD="$ALPHA/build-linux"
DEPS="$BUILD/deps"
DXVK_VERSION=v3.1.1
FAUDIO_VERSION=26.10

mkdir -p "$DEPS"

if [ ! -e "$DEPS/dxvk-native/lib/libdxvk_d3d9.so.0" ]; then
    echo "== DXVK-native $DXVK_VERSION"
    [ -d "$DEPS/dxvk-src" ] || git clone --depth 1 --branch "$DXVK_VERSION" --recurse-submodules \
        --shallow-submodules https://github.com/doitsujin/dxvk.git "$DEPS/dxvk-src"
    # Direct3D 9 only, drawing into SDL3 windows.
    meson setup --wipe "$DEPS/dxvk-build" "$DEPS/dxvk-src" --buildtype=release \
        --prefix="$DEPS/dxvk-native" --libdir=lib \
        -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false \
        -Dnative_sdl3=enabled -Dnative_sdl2=disabled -Dnative_glfw=disabled
    ninja -C "$DEPS/dxvk-build" install
fi

if [ ! -e "$DEPS/FAudio/lib/libFAudio.so.0" ]; then
    echo "== FAudio $FAUDIO_VERSION"
    [ -d "$DEPS/FAudio-src" ] || git clone --depth 1 --branch "$FAUDIO_VERSION" \
        https://github.com/FNA-XNA/FAudio.git "$DEPS/FAudio-src"
    cmake -S "$DEPS/FAudio-src" -B "$DEPS/FAudio-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS/FAudio" -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_SDL3=ON
    cmake --build "$DEPS/FAudio-build"
    cmake --install "$DEPS/FAudio-build"
fi

echo "== Cube and Server"
cmake -S "$ALPHA" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCUBE_SCRIPTING=OFF \
    -DDXVK_NATIVE_DIR="$DEPS/dxvk-native" -DFAUDIO_DIR="$DEPS/FAudio"
cmake --build "$BUILD"

if [ -z "$1" ]; then
    echo
    echo "Built $BUILD/Cube and $BUILD/Server."
    echo "For a playable folder, run again with your Cube World Alpha folder as argument."
    exit 0
fi

GAME="$(cd "$1" && pwd)"
TARGET="${2:-$BUILD/game}"
DATA="data1.db data2.db data3.db data4.db db.dat resource1.dat resource2.dat logo.bmp
      cursor.plx gui.plx help.plx quest-tag.plx start.plx"
for file in $DATA; do
    if [ ! -f "$GAME/$file" ]; then
        echo "$GAME/$file is missing: give the folder of your Cube World Alpha (with Cube.exe)." >&2
        exit 1
    fi
done

echo "== Game folder $TARGET"
mkdir -p "$TARGET/lib" "$TARGET/Save"
# Replaced by renaming, so a running game keeps its files.
for program in Cube Server; do
    cp "$BUILD/$program" "$TARGET/.$program.new" && mv "$TARGET/.$program.new" "$TARGET/$program"
done
for library in "$DEPS/dxvk-native/lib/libdxvk_d3d9.so.0" "$DEPS/FAudio/lib/libFAudio.so.0"; do
    name="$(basename "$library")"
    cp -L "$library" "$TARGET/lib/.$name.new" && mv "$TARGET/lib/.$name.new" "$TARGET/lib/$name"
done
for file in $DATA; do
    cp "$GAME/$file" "$TARGET/"
done
# The player's settings stay; the first build takes those of the Windows installation.
[ -f "$TARGET/options.cfg" ] || [ ! -f "$GAME/options.cfg" ] || cp "$GAME/options.cfg" "$TARGET/"

echo
echo "Done. Start the game with:  \"$TARGET/Cube\""
echo "Dedicated server (port 12345, q quits):  cd \"$TARGET\" && ./Server"
