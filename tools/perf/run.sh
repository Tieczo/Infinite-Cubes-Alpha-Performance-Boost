#!/bin/bash
# Test-only measuring run (tools/README.md). Never touches a game folder anyone plays from.
#   run.sh BIN SCENE RD CD PD SECONDS LABEL [SAMPLE_AFTER [SPIN]]
#   BIN    test build from build-measure.sh (prof.h measuring points, testinput-perf.h)
#   Env (required): PERF_TMP (work folder), CW_GAME_DIR (a playable native Linux folder from
#          alpha/platform/linux/build.sh; data files and lib/ are copied from it), CW_SCENES_DIR (test scenes).
#   Env (optional): CW_LOCK (lock file shared by every measuring run on the machine), HEADLESS=0 (a desktop window
#          instead of gamescope --backend headless), SCENE_TIME_MS (start clock), COMBAT=1 (attack rhythm; GOD=1,
#          WALK_SECONDS=n), EXTRA_OPTIONS, SPINSHOT=1
#   SCENE  a folder in CW_SCENES_DIR with Save/ (copy of a Save folder) and scene.cfg ("WORLD SLOT [SEED]"); rebuilt
#          for every run: only that world and character, world clock 11:30; the clock runs 10x, so the window lands
#          around noon. SCENE_WORLD / SCENE_SLOT / SCENE_SEED override scene.cfg.
#   RD/CD/PD  render / creature / plant distance in %
#   SECONDS   time in the world after the menus; SAMPLE_AFTER: seconds after which the sampler and the
#             camera spin start (default 0 = sampler off, no spin); SPIN: degrees per second (default 36,
#             one turn per 10-s PROF window)
# Window (not fullscreen) titled "TESTLAUF (Claude) - nicht spielen" (test run, do not play), 2560x1440 back buffer,
# 8x AA, no FPS limit, sound off. One run at a time on the whole machine (flock on $CW_LOCK).
# Output: $PERF_TMP/logs/LABEL.log (PROF blocks every 10 s), LABEL.samples, LABEL.ppm (last frame).
set -u
HERE=$(cd "$(dirname "$0")" && pwd); W=$HERE
: "${PERF_TMP:?set PERF_TMP to a work folder for the test folder and logs}"
: "${CW_GAME_DIR:?set CW_GAME_DIR to a playable native Linux folder (alpha/platform/linux/build.sh "<Cube World folder>" DIR)}"
: "${CW_SCENES_DIR:?set CW_SCENES_DIR to the test scenes folder (SCENE/Save and SCENE/scene.cfg, see tools/README.md)}"
LOCK=${CW_LOCK:-${XDG_RUNTIME_DIR:-/tmp}/infinite-cubes-measure.lock}
[ -f "$CW_GAME_DIR/data1.db" ] && [ -d "$CW_GAME_DIR/lib" ] || { echo "CW_GAME_DIR has no data1.db or lib/: $CW_GAME_DIR"; exit 2; }
BIN=$1 SCENE=$2 RD=$3 CD=$4 PD=$5 SECS=$6 LABEL=$7 SAMPLE_AFTER=${8:-0} SPIN=${9:-36}
SRC=$CW_SCENES_DIR/$SCENE/Save; WORLD= SLOT= SEED=
[ -d "$SRC" ] || { echo "unknown scene: $SRC missing"; exit 2; }
[ -f "$CW_SCENES_DIR/$SCENE/scene.cfg" ] && read -r WORLD SLOT SEED < "$CW_SCENES_DIR/$SCENE/scene.cfg"
WORLD=${SCENE_WORLD:-$WORLD} SLOT=${SCENE_SLOT:-${SLOT:-0}} SEED=${SCENE_SEED:-$SEED}
[ -n "$WORLD" ] || { echo "scene $SCENE: world name unknown (scene.cfg or SCENE_WORLD)"; exit 2; }
# The user's own game (any Cube not started from a test folder) means: no measuring.
user_playing() {
  for p in $(pgrep -x Cube); do
    case "$(readlink /proc/$p/cwd)" in "$PERF_TMP"/*|/tmp/*) ;; *) return 0 ;; esac
  done
  return 1
}
user_playing && { echo "a Cube outside the test folders is running (someone plays) - no run ($LABEL)"; exit 1; }
exec 9>"$LOCK"
flock -w 900 9 || { echo "$LOCK busy for 15 min - given up ($LABEL)"; exit 1; }
user_playing && { echo "a Cube outside the test folders is running (someone plays) - no run ($LABEL)"; exit 1; }
T=$PERF_TMP/t
if [ ! -f "$T/data1.db" ]; then
  mkdir -p "$T"
  (cd "$CW_GAME_DIR" && cp -a cursor.plx data1.db data2.db data3.db data4.db db.dat gui.plx help.plx lib logo.bmp \
     quest-tag.plx resource1.dat resource2.dat start.plx "$T/")
fi
[ -p "$T/fifo" ] || mkfifo "$T/fifo"
export TESTDIR=$T; cd "$T" || exit 1
cp "$BIN" ./Cube
rm -rf ./Save
# The mod reads options-InfiniteCubes.cfg first (made from options.cfg on the first start): drop it, so options.cfg counts.
rm -f options-InfiniteCubes.cfg
# SCENE_TIME_MS: world clock at the start (default 11:30 = 41400000; e.g. 22:00 = 79200000 for owls at night)
python3 -I "$HERE/prep_scene.py" "$SRC" "$T/Save" $WORLD $SLOT ${SCENE_TIME_MS:-41400000} || exit 1
cat > options.cfg <<OPT
fullscreen 0
resolutionX 2560
resolutionY 1440
antiAliasing ${AA:-3}
renderDistance $RD
soundVolume 0
musicVolume 0
cameraSpeed 10
cameraSmoothness 65
language 0
invertY 0
minTimeStep 0
showFPS 1
plantDistance $PD
creatureDistance $CD
fastCreatureUpdates 1
fastTerrainLoading 1
OPT
# EXTRA_OPTIONS: further options.cfg lines, e.g. "fastPlantScan 0" (one per line)
[ -n "${EXTRA_OPTIONS:-}" ] && printf '%s\n' "$EXTRA_OPTIONS" >> options.cfg
mkdir -p "$PERF_TMP/logs"; log=$PERF_TMP/logs/$LABEL.log; : > "$log"
rm -f "$T/sample.flag"
# The back buffer as the user plays (2560x1440); CUBE_TEST_PIXELS= (empty) = maximized window as before 28.10. evening.
export CUBE_TEST_PIXELS=${CUBE_TEST_PIXELS-2560x1440}
export CUBE_PROF_SAMPLES=$PERF_TMP/logs/$LABEL.samples CUBE_PROF_SAMPLE_FLAG=$T/sample.flag
echo "RUN $LABEL pixels=${CUBE_TEST_PIXELS:-max} aa=${AA:-3} pitch=${PITCH:-80} combat=${COMBAT:-0} chase=${CHASE:-0} mob=${MOB:-0} time=${SCENE_TIME_MS:-41400000} extra=[${EXTRA_OPTIONS:-}] scene=$SCENE rd=$RD cd=$CD pd=$PD secs=$SECS sample_after=$SAMPLE_AFTER spin=$SPIN bin=$BIN $(date +%T)" >> "$log"
# SERVER=path: a local Server next to the game, in its own folder, on the scene's seed (scene.cfg or SCENE_SEED). The game connects during
# the run ("connect", "pin"); MOB waves then come from the Server (mob.cmd). Its log: LABEL.server.log.
spid=
if [ -n "${SERVER:-}" ]; then
  SV=$PERF_TMP/srv; rm -rf "$SV"; mkdir -p "$SV/Save"
  for f in cursor.plx data1.db data2.db data3.db data4.db db.dat gui.plx help.plx lib logo.bmp quest-tag.plx resource1.dat resource2.dat start.plx; do ln -s "$T/$f" "$SV/$f"; done
  [ -n "$SEED" ] || { echo "SERVER needs the world's seed (scene.cfg or SCENE_SEED)"; exit 2; }
  cp "$SERVER" "$SV/Server"; printf '%s' "$SEED" > "$SV/server.cfg"
  (cd "$SV" && exec ./Server < <(sleep 900)) > "$PERF_TMP/logs/$LABEL.server.log" 2>&1 & spid=$!
fi
# HEADLESS=1 (default): invisible in gamescope, so nobody takes the window for a game (fps only comparable with headless runs).
GS=(); if [ "${HEADLESS:-1}" = 1 ]; then
  command -v gamescope > /dev/null || { echo "gamescope missing (or HEADLESS=0)"; exit 2; }
  GS=(env -u WAYLAND_DISPLAY gamescope --backend headless -W 2560 -H 1440 -w 2560 -h 1440 -r 1000 --expose-wayland --)
  echo "HEADLESS gamescope hz=1000" >> "$log"
fi
CUBE_TEST_FIFO=$T/fifo timeout $((SECS + 90 + ${ABSHOTS:-0} * 60 + ${SERVER:+60}+0)) "${GS[@]}" ./Cube >> "$log" 2>&1 & tpid=$!
# The game's own process (under gamescope it is no child of the shell): found by its working folder.
cube_pids() { for q in $(pgrep -x Cube); do [ "$(readlink /proc/$q/cwd)" = "$T" ] && echo $q; done; }
for i in $(seq 60); do grep -q 'read cursor.plx' "$log" && break; sleep 1; done; sleep 4
# The menus are centred: the clicks (measured at 2560x1440) shift by half the size difference.
ok=0; dy=0
if $W/waitscreen start 20; then
  rm -f "$PERF_TMP/logs/$LABEL-size.ppm"; $W/t shot "$PERF_TMP/logs/$LABEL-size.ppm"
  for i in $(seq 20); do [ -s "$PERF_TMP/logs/$LABEL-size.ppm" ] && break; sleep 0.25; done; sleep 0.5
  size=$(head -c 20 "$PERF_TMP/logs/$LABEL-size.ppm" | sed -n 2p); rm -f "$PERF_TMP/logs/$LABEL-size.ppm"
  echo "BACKBUFFER $size" >> "$log"; w=${size% *}; h=${size#* }; dx=$(( (2560 - w) / 2 )); dy=$(( (1440 - h) / 2 ))
  $W/fclick $((1280 - dx)) $((794 - dy)) 2 && $W/waitscreen characters 8 && $W/fclick $((1280 - dx)) $((608 - dy)) 1.5 && \
    $W/fclick $((1460 - dx)) $((660 - dy)) 2 && $W/waitscreen worlds 8 && $W/fclick $((1280 - dx)) $((608 - dy)) 1.5 && \
    $W/fclick $((1460 - dx)) $((660 - dy)) 2 && ok=1
fi
[ $ok = 1 ] || { echo "MENU FAILED ($LABEL)"; $W/t shot "$PERF_TMP/logs/$LABEL-menu.ppm"; sleep 2; kill -TERM $tpid; wait $tpid; exit 1; }
echo "WORLD $(date +%T)" >> "$log"
[ "${COMBAT:-0}" = 1 ] || $W/t move $((1280 - dx)) $((720 - dy))   # no mouse at all before a combat run's walk
# PITCH: camera tilt set after entering the world, the same in every scene (90 = level; default 80, a little
# down), so that a saved view at the ground does not decide what is drawn.
sleep 1; $W/t pitch "${PITCH:-80}"
# Combat runs: FACE=none (default: the camera as loaded), faceenemy (turned towards the nearest enemy) or
# facechar (where the character looks); W then walks that way. WALK_SECONDS=auto finds the walk (see below).
[ "${COMBAT:-0}" = 1 ] && [ "${FACE:-none}" != none ] && $W/t "${FACE:-none}"
# YAW=degrees: a fixed camera turn for every run (instead of the loaded one), e.g. measured once with CAMSTATE.
[ -n "${YAW:-}" ] && $W/t yaw "$YAW"
sleep 0.5; $W/t camstate geladen
# TERRAIN=1: a height map around the player into the log (to choose MOB_X, MOB_Y).
[ "${TERRAIN:-0}" = 1 ] && { sleep 45; $W/t terrain 240 8; sleep 2; }
# MOB=n (load test, with COMBAT=1 GOD=1 CHASE=1): after 60 s n fixed enemies around MOB_X, MOB_Y, the player goes
# there and stands; the measuring window opens after SAMPLE_AFTER as usual. Not comparable with CHASE runs.
mob_wave() {
  if [ -n "${SERVER:-}" ]; then echo "$MOB $MOB_X $MOB_Y ${MOB_HP:-10000}" > "$SV/mob.tmp"; mv "$SV/mob.tmp" "$SV/mob.cmd"
  else $W/t mob "$MOB" "$MOB_X" "$MOB_Y" ${MOB_HP:-}; fi
}
if [ -n "${SERVER:-}" ]; then   # 90 s: connect, then the new world loads around the same column
  sleep 30; $W/t god 1; $W/t connect 127.0.0.1; sleep 6; $W/t pin "$MOB_X" "$MOB_Y"; sleep 54; SAMPLE_AFTER=$((SAMPLE_AFTER - 90))
fi
if [ "${MOB:-0}" != 0 ]; then
  [ -n "${SERVER:-}" ] || { sleep 60; SAMPLE_AFTER=$((SAMPLE_AFTER - 60)); }
  $W/t god 1; mob_wave; sleep 1; $W/t chase 1; SAMPLE_AFTER=$((SAMPLE_AFTER - 1))
fi
# Every thread's CPU time (clock ticks) for the log, at the start and end of the measuring window.
threadcpu() {
  local p d s r; p=$(cube_pids | head -n 1); [ -n "$p" ] || return 0
  echo "THREADCPU $1 now=$(date +%s.%N)" >> "$log"
  for d in /proc/$p/task/*; do
    s=$(cat "$d/stat" 2>/dev/null) || continue; r=${s##*) }; set -- $r
    echo "THREADCPU tid=${d##*/} comm=$(tr ' ' _ < "$d/comm") ticks=$((${12} + ${13}))" >> "$log"
  done
}
if [ "$SAMPLE_AFTER" -gt 0 ]; then
  sleep "$SAMPLE_AFTER"; echo "MEASURE $(date +%T)" >> "$log"; touch "$T/sample.flag"; threadcpu start
  [ "${COMBAT:-0}" = 1 ] || $W/t spin "$SPIN"   # combat runs start the spin after the walk
  # WANDER=seed: like a player (walk, jump, climb, look around, quick swings, a fight now and then; testinput-perf.h).
  # Use with SPIN 0, without COMBAT.
  [ -n "${WANDER:-}" ] && { $W/t god 1; $W/t wander "$WANDER"; }
  # DRIFT="vx vy z": the player flies through new terrain during the measuring window.
  [ -n "${DRIFT:-}" ] && $W/t drift $DRIFT
  # proof of the turn: one picture after a quarter turn (the last one is taken at the end)
  if [ "${SPINSHOT:-0}" = 1 ]; then sleep 2.5; rm -f "$PERF_TMP/logs/$LABEL-spin.ppm"; $W/t shot "$PERF_TMP/logs/$LABEL-spin.ppm"; sleep 0.5; fi
  # COMBAT=1: a fixed attack rhythm for the whole measuring window, the same in every run: the left button
  # held 1.5 s (basic attacks one after the other), then the right button 0.3 s (class ability, e.g. the
  # mage's fire explosion); the camera spin goes on meanwhile. Without enemies nearby this measures the
  # attacks, projectiles and effects, not hits.
  if [ "${COMBAT:-0}" = 1 ]; then
    # GOD=1: the player cannot die (test build sets only the player's hit points to the maximum every frame;
    # enemies, hits, damage numbers and deaths stay as they are). WALK_SECONDS=n: first walk forward (W) for
    # n seconds, e.g. from the saved spot to the enemy group, without camera spin, then the spin starts again.
    [ "${GOD:-0}" = 1 ] && $W/t god 1
    if [ "${WALK_SECONDS:-0}" != 0 ]; then
      [ "${FACE:-none}" != none ] && { $W/t "${FACE:-none}"; sleep 0.3; }
      [ -n "${YAW:-}" ] && { $W/t yaw "$YAW"; sleep 0.3; }
      $W/t camstate vorAnlauf
      if [ "$WALK_SECONDS" = auto ]; then
        # Find the walk once: 3 s, then 1 s more while no enemy has hit the player within 2 s, at most twice.
        hits0=$(grep -c PLAYERHIT "$log")   # hits since the walk started count
        walked=3; $W/t kd W; sleep 3; $W/t ku W; sleep 2
        while [ "$(grep -c PLAYERHIT "$log")" = "$hits0" ] && [ $walked -lt 5 ]; do
          $W/t kd W; sleep 1; $W/t ku W; walked=$((walked + 1)); sleep 2
        done
        echo "WALK auto=$walked s hits=$(grep -c PLAYERHIT "$log")" >> "$log"
      else
        $W/t kd W; sleep "$WALK_SECONDS"; $W/t ku W
      fi
      sleep 0.5; $W/t camstate nachAnlauf
    fi
    # CHASE=1 (real combat): instead of the spin the test input turns every frame to the nearest living
    # enemy and walks up to melee range (not deep into water); the attack rhythm below stays the same.
    # Use with WALK_SECONDS=0. CHASE=0 (default) is the old variant (A/B).
    if [ "${CHASE:-0}" = 1 ]; then $W/t chase 1; else $W/t spin "$SPIN"; fi
    end=$(( $(date +%s) + SECS - SAMPLE_AFTER - ${SPINSHOT:-0} * 3 ))
    while [ "$(date +%s)" -lt "$end" ]; do
      $W/t down 1; sleep 1.5; $W/t up 1; sleep 0.1
      $W/t down 3; sleep 0.3; $W/t up 3; sleep 0.1
      # MOB_WAVE=n: a new wave every n seconds (with MOB_HP, enemies that can die).
      if [ -n "${MOB_WAVE:-}" ] && [ $(( $(date +%s) - ${wave_at:=$(date +%s)} )) -ge "$MOB_WAVE" ]; then
        wave_at=$(date +%s); mob_wave
      fi
    done
  else
    sleep $((SECS - SAMPLE_AFTER - ${SPINSHOT:-0} * 3))
  fi
else
  sleep "$SECS"
fi
threadcpu ende
# WORLDHASH=1: at the end the hashes of the loaded zones (test command worldhash) go to the log.
if [ "${WORLDHASH:-0}" = 1 ]; then $W/t worldhash 6; sleep 3; fi
# SHOT=1: a picture of the last frame (11-24 MB each in /tmp, and that frame takes ~90 ms); off by default.
if [ "${SHOT:-0}" = 1 ]; then rm -f "$PERF_TMP/logs/$LABEL.ppm"; $W/t shot "$PERF_TMP/logs/$LABEL.ppm"; sleep 2; fi
# ABSHOTS=1 (build with ab_patch.py): game time stopped, pictures old, new, old, new and one without the region, in this
# process, into logs/LABEL-ab (ab_compare.py).
if [ "${ABSHOTS:-0}" = 1 ]; then
  O=$PERF_TMP/logs/$LABEL-ab; mkdir -p "$O"; rm -f "$O"/*.ppm
  $W/t fx; $W/t freeze 1; sleep 3
  for s in "1 alt1" "0 neu1" "1 alt2" "0 neu2" "2 leer"; do set -- $s
    $W/t abold $1; sleep 2; $W/t shot "$O/$2.ppm"; for i in $(seq 20); do [ -s "$O/$2.ppm" ] && break; sleep 0.25; done; sleep 1
  done
fi
for q in $(cube_pids); do kill -TERM $q; done
for i in $(seq 40); do cube_pids | grep -q . || break; sleep 0.25; done
kill -TERM $tpid 2>/dev/null; wait $tpid
[ -n "$spid" ] && { kill -TERM $spid 2>/dev/null; wait $spid 2>/dev/null; }
echo "done: $LABEL"
