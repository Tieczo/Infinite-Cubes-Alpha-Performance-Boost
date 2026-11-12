# tools

Test and measuring scripts (Linux). They patch throwaway copies of `alpha/`; nothing here goes into the game.
No game data, saves or scenes are included: you need your own Cube World Alpha.

## Environment

| Variable | Meaning |
| --- | --- |
| `PERF_TMP` | work folder for test folders and logs (`$PERF_TMP/t`, `$PERF_TMP/logs`) |
| `CW_GAME_DIR` | a playable native Linux folder from `alpha/platform/linux/build.sh "<Cube World folder>" DIR`; data files and `lib/` are copied from it |
| `CW_ORIGINAL_DIR` | your own Cube World Alpha folder (`Cube.exe`, `Server.exe`), for the multiplayer tests; only copied from |
| `CW_SCENES_DIR` | test scenes: `SCENE/Save/` (copy of a `Save` folder) and `SCENE/scene.cfg` = `WORLD SLOT [SEED]` (world name, character slot, the world's seed for `SERVER=` runs) |
| `CW_LOCK` | lock file every measuring run on the machine shares (default `$XDG_RUNTIME_DIR/infinite-cubes-measure.lock`) |
| `DXVK_NATIVE_DIR`, `FAUDIO_DIR`, `CW_DEPS_DIR` | for `build-measure.sh`; default: what `build.sh` put in `alpha/build-linux/deps` and `_deps` (offline zlib/FreeImage sources; never a MinGW build's) |

Suggested scenes (record them once with your build and keep them unchanged): `normal` (high ground with a far view),
`kampf` (a lake shore with ~6 enemies in reach), the mass-combat load test uses `kampf` with `MOB=30`.

## perf/

- `build-measure.sh COMMIT DEST [extra-patch.py ...]` → `DEST/Cube`: profiler zones (`prof.h` via `patch_prof.py`), test
  input channel (`testinput-perf.h` via `hook.py`), window title "TESTLAUF (Claude) - nicht spielen". `AB_BASE=commit`
  adds the A/B switch (`ab_patch.py`, list your regions there first).
- `run.sh BIN SCENE RD CD PD SECONDS LABEL [SAMPLE_AFTER [SPIN]]`: one run under `flock $CW_LOCK`, headless in gamescope
  (`HEADLESS=0` for a window), 2560x1440 back buffer, 8x AA (`AA=2` = 4x), no fps limit, clock 11:30, camera turns 36°/s
  from `SAMPLE_AFTER`. Refuses to run while a Cube outside the test folders runs. Options in the script header:
  `COMBAT=1 GOD=1 CHASE=1` (real fight), `MOB=n MOB_X MOB_Y` (load test), `SERVER=path` (through a local server),
  `WORLDHASH=1`, `ABSHOTS=1`, `WANDER=seed`, `DRIFT`, `EXTRA_OPTIONS`.
- `summarize.py LOGS... [--zones]`: fps, p50/p99/max per run, mean and range per label (label without `-N`).
- `symbolize.py LABEL.samples BIN`: where the main thread's time goes (functions, `render()` lines).
- `worldhash_compare.py LOGS...`: the generated world is identical across builds (run with `WORLDHASH=1`, without fights).
- `ab_compare.py DIR`: `PIXELGLEICH` when old and new pictures of one `ABSHOTS=1` run are identical; else `diff.ppm`.
- `shadow_check_example.py`: template of a shadow check (old and new code on the same state each frame, `rand()` replayed).
- `t`, `fclick`, `waitscreen`: send commands to a running test build (`TESTDIR` = folder with its `fifo`).

Typical (400 %): `run.sh BIN normal 400 400 100 170 v1-normal-1 110`. Fight:
`COMBAT=1 GOD=1 CHASE=1 run.sh BIN kampf 400 400 100 170 v1-kampf-1 110`. Mass combat:
`MOB=30 MOB_X=… MOB_Y=… COMBAT=1 GOD=1 CHASE=1 run.sh BIN kampf 400 400 100 150 v1-mob-1 110` (`TERRAIN=1` once to pick
`MOB_X/MOB_Y`). 200 %: 100 s, measuring from 50.

## multiplayer/

- `setup.sh MP SCENE`: work folder with copies (`orig-srv`, `orig-cli`, `lin-srv`, `lin-cli`), its own Wine prefix
  (or a copy of `CW_WINEPREFIX_TEMPLATE`) and `hand.exe`. Needs `MEASURE_BIN` (a `build-measure.sh` Cube).
- `join.sh MP orig|lin SEED LABEL [SECONDS]`: our Linux test client on the original Server.exe (`orig`) or our server
  (`lin`), with an observer; results in `MP/res-LABEL`.
- `both.sh MP orig|lin SEED LABEL`: the original Cube.exe (Wine, invisible) and our client on the same server.
- `observer.py` (second player without graphics: sees players, counts hits, kills and doubled hits), `chat_check.py`
  (version check, non-ASCII chat), `map_shape.py`/`map_regions.py` (compare `map_online_<seed>.db` of two clients),
  `xt.py` (XTest input into gamescope), `hand.c` (input for Cube.exe inside Wine).
