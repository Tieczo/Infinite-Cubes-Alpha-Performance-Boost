#!/bin/bash
# Builds the work folder for both.sh and join.sh (tools/README.md). Copies only; your Cube World folder, your game
# folders and your Wine prefix stay untouched.
#   setup.sh MP SCENE
#   MP     new work folder (outside your game folders)
#   SCENE  a scene folder in CW_SCENES_DIR (Save/ and scene.cfg "WORLD SLOT [SEED]"), used by both test clients
# Env (required): CW_ORIGINAL_DIR (your own Cube World Alpha folder with Cube.exe and Server.exe), CW_GAME_DIR (a playable
#   native Linux folder from alpha/platform/linux/build.sh), CW_SCENES_DIR, MEASURE_BIN (Cube from ../perf/build-measure.sh)
# Env (optional): LIN_SERVER (default $CW_GAME_DIR/Server), CW_WINEPREFIX_TEMPLATE (a prefix to copy; default: a new
#   one from wineboot, Direct3D through wined3d). Needs wine, gamescope, python3-xlib; i686-w64-mingw32-gcc for hand.exe.
set -eu
MP=$1 SCENE=$2
HERE=$(cd "$(dirname "$0")" && pwd)
: "${CW_ORIGINAL_DIR:?set CW_ORIGINAL_DIR to your own Cube World Alpha folder (Cube.exe, Server.exe, data files)}"
: "${CW_GAME_DIR:?set CW_GAME_DIR to a playable native Linux folder (alpha/platform/linux/build.sh)}"
: "${CW_SCENES_DIR:?set CW_SCENES_DIR to the test scenes folder}"
: "${MEASURE_BIN:?set MEASURE_BIN to a Cube built by tools/perf/build-measure.sh}"
[ -f "$CW_ORIGINAL_DIR/Cube.exe" ] && [ -f "$CW_ORIGINAL_DIR/Server.exe" ] || { echo "no Cube.exe/Server.exe in $CW_ORIGINAL_DIR"; exit 2; }
[ -d "$CW_SCENES_DIR/$SCENE/Save" ] || { echo "no scene $CW_SCENES_DIR/$SCENE/Save"; exit 2; }
[ -e "$MP" ] && { echo "$MP exists already"; exit 1; }
WORLD= SLOT=; read -r WORLD SLOT _ < "$CW_SCENES_DIR/$SCENE/scene.cfg"
mkdir -p "$MP"; MP=$(cd "$MP" && pwd)
printf 'export WINEPREFIX=%q WINEDEBUG=-all\n' "$MP/prefix" > "$MP/env.sh"
printf 'WORLD=%q SLOT=%q\n' "$WORLD" "${SLOT:-0}" > "$MP/scene.env"
if [ -n "${CW_WINEPREFIX_TEMPLATE:-}" ]; then cp -a "$CW_WINEPREFIX_TEMPLATE" "$MP/prefix"
else ( . "$MP/env.sh"; wineboot -i > /dev/null 2>&1 ); fi
for d in orig-srv orig-cli; do mkdir "$MP/$d"; (cd "$CW_ORIGINAL_DIR" && tar -c --exclude=./Save .) | tar -x -C "$MP/$d"; mkdir -p "$MP/$d/Save"; done
DATA="cursor.plx data1.db data2.db data3.db data4.db db.dat gui.plx help.plx lib logo.bmp quest-tag.plx resource1.dat resource2.dat start.plx"
for d in lin-srv lin-cli; do mkdir "$MP/$d"; (cd "$CW_GAME_DIR" && cp -a $DATA "$MP/$d/"); done
cp "${LIN_SERVER:-$CW_GAME_DIR/Server}" "$MP/lin-srv/Server"; mkdir -p "$MP/lin-srv/Save"
cp "$MEASURE_BIN" "$MP/lin-cli/Cube"; mkfifo "$MP/lin-cli/fifo"
cp -a "$CW_SCENES_DIR/$SCENE/Save" "$MP/orig-saves"
python3 -I "$HERE/../perf/prep_scene.py" "$MP/orig-saves" "$MP/lin-cli/Save" "$WORLD" "${SLOT:-0}" 41400000 > /dev/null
if command -v i686-w64-mingw32-gcc > /dev/null; then i686-w64-mingw32-gcc -O2 -o "$MP/orig-cli/hand.exe" "$HERE/hand.c" -luser32 -lgdi32
else echo "note: no i686-w64-mingw32-gcc, both.sh runs without hand.exe"; fi
echo "work folder ready: $MP"
