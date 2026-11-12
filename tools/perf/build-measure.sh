#!/bin/bash
# Test-only: builds a measuring build of a commit for run.sh (tools/README.md).
#   build-measure.sh COMMIT DEST_DIR [extra patch script ...]
# Extra patch scripts get <DEST_DIR>/alpha as argument (e.g. p1_check.py, verhalten_patch.py).
# Result: DEST_DIR/Cube (prof.h measuring points, testinput-perf.h, test clicks in back-buffer pixels,
# window title "TESTLAUF (Claude) - nicht spielen"). Builds with nice -n 10 and -j8.
# Env: DXVK_NATIVE_DIR, FAUDIO_DIR (default: what alpha/platform/linux/build.sh put in alpha/build-linux/deps),
# AB_BASE=commit (A/B build: ab_patch.py puts that commit's code of its regions next to the new one), LTO, CXXFLAGS_EXTRA.
# Offline when alpha/build-linux/_deps (or CW_DEPS_DIR) holds the zlib and FreeImage sources of an earlier Linux build
# (never those of a MinGW build: its configure patches FreeImage for Windows).
set -eu
COMMIT=$1 DEST=$2; shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(git -C "$HERE" rev-parse --show-toplevel)
DEPS=${CW_DEPS_DIR:-$REPO/alpha/build-linux/_deps}
DXVK=${DXVK_NATIVE_DIR:-$REPO/alpha/build-linux/deps/dxvk-native} FAUDIO=${FAUDIO_DIR:-$REPO/alpha/build-linux/deps/FAudio}
[ -d "$DXVK/lib" ] || { echo "no DXVK-native at $DXVK: run alpha/platform/linux/build.sh once or set DXVK_NATIVE_DIR"; exit 2; }
OFFLINE=(); [ -d "$DEPS/zlib_source-src" ] && [ -d "$DEPS/freeimage_source-src" ] && OFFLINE=(
  -DFETCHCONTENT_SOURCE_DIR_ZLIB_SOURCE="$DEPS/zlib_source-src"
  -DFETCHCONTENT_SOURCE_DIR_FREEIMAGE_SOURCE="$DEPS/freeimage_source-src" -DFETCHCONTENT_FULLY_DISCONNECTED=ON)
[ -e "$DEST/alpha" ] && { echo "$DEST/alpha exists already"; exit 1; }
mkdir -p "$DEST"
git -C "$REPO" archive "$COMMIT" alpha | tar -x -C "$DEST"
A=$DEST/alpha
python3 -I "$HERE/patch_prof.py" "$A" "$HERE/prof.h"
if [ -n "${AB_BASE:-}" ]; then
  git -C "$REPO" show "$AB_BASE:alpha/src/cube/GameControllerRender.cpp" > "$DEST/base-render.cpp"
  python3 -I "$HERE/ab_patch.py" "$A" "$DEST/base-render.cpp"
fi
TESTINPUT=$HERE/testinput-perf.h python3 -I "$HERE/hook.py" "$A/platform/linux/main.cpp"
python3 -I - "$A/platform/linux/main.cpp" <<'EOF'
import sys; p = sys.argv[1]; s = open(p).read()
for a, b in [('    if (t_mouseSet) { x = t_mx; y = t_my; }\n    int width = 0, height = 0;',
              '    if (t_mouseSet) { x = t_mx; y = t_my; return; } // test: back-buffer pixels in any window mode\n    int width = 0, height = 0;'),
             ('SDL_CreateWindow("Cube World", 800, 600,', 'SDL_CreateWindow("TESTLAUF (Claude) - nicht spielen", 800, 600,'),
             ('    SDL_MaximizeWindow(g_window);\n',
              '''    // test: CUBE_TEST_PIXELS=WxH gives the window that many pixels (the back buffer) instead of maximizing it
    if (const char *want = getenv("CUBE_TEST_PIXELS")) {
        int ww = 0, wh = 0;
        if (sscanf(want, "%dx%d", &ww, &wh) == 2) {
            float density = SDL_GetWindowPixelDensity(g_window);
            if (density <= 0.0f) density = 1.0f;
            int lw = (int)(ww / density + 0.5f), lh = (int)(wh / density + 0.5f);
            for (int tries = 0; tries < 4; tries++) {
                SDL_SetWindowSize(g_window, lw, lh);
                SDL_SyncWindow(g_window);
                int pw = 0, ph = 0;
                SDL_GetWindowSizeInPixels(g_window, &pw, &ph);
                if (pw == ww && ph == wh) break;
                lw += pw < ww ? 1 : pw > ww ? -1 : 0;
                lh += ph < wh ? 1 : ph > wh ? -1 : 0;
            }
        } else SDL_MaximizeWindow(g_window);
    } else
    SDL_MaximizeWindow(g_window);
''')]:
    assert s.count(a) == 1, a
    s = s.replace(a, b)
open(p, 'w').write(s)
EOF
for extra in "$@"; do python3 -I "$extra" "$A"; done
# LTO=1: link-time optimisation; CXXFLAGS_EXTRA="...": more compiler flags (e.g. "-march=native -ffp-contract=off").
cmake -S "$A" -B "$A/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCUBE_SCRIPTING=OFF \
  -DCMAKE_CXX_FLAGS="-g ${CXXFLAGS_EXTRA:-}" -DCMAKE_C_FLAGS="${CXXFLAGS_EXTRA:-}" \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=$([ "${LTO:-0}" = 1 ] && echo ON || echo OFF) \
  -DDXVK_NATIVE_DIR="$DXVK" -DFAUDIO_DIR="$FAUDIO" "${OFFLINE[@]}" \
  > "$DEST/cmake.log" 2>&1 || { tail -20 "$DEST/cmake.log"; exit 1; }
nice -n 10 ninja -j8 -C "$A/build" Cube > "$DEST/build.log" 2>&1 || { grep -E "error" -A3 "$DEST/build.log" | head -30; exit 1; }
cp "$A/build/Cube" "$DEST/Cube"
echo "measuring build ready: $DEST/Cube ($(git -C "$REPO" log --oneline -1 "$COMMIT"))"
