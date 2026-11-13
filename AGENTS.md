# AGENTS.md

Infinite Cubes Alpha Performance Boost: a performance mod of coremaze's Cube World Alpha reconstruction (`alpha/`).
User-facing description: [PERFORMANCE.md](PERFORMANCE.md). The code is the documentation; this file holds the rules,
pointers and the lessons that were expensive to learn. Only `alpha/` is ours (`release/`, `plasma/` are upstream).

## Rules (non-negotiable)

1. **Gameplay and enemies 1:1 like the original Cube.exe / Server.exe** (AI, pathing, timing, spawns, damage, world
   generation). The yardstick is the original programs, not our previous build: deviations found are corrected back to
   the original, one commit each, with proof against the original programs.
2. **No added input lag.** Anything that costs latency is a switch, default off, latency stated (Threaded Rendering:
   up to ~2.6 ms, off).
3. **No settings that need a restart.** Every option takes effect at once.
4. **Multiplayer with the original Cube.exe and Server.exe in both directions.** Protocol, packet format and world
   generation stay unchanged. Test after any network or server change (skill `multiplayer-test`).
5. **Even frame times before peak fps.** p99/p99.9 and spikes in big fights and while terrain loads matter more than the mean.
6. **Gameplay changes only as an option, default off** (example: `guardianReliableBlock`).
7. **Own options file `options-InfiniteCubes.cfg`** (`Options.cpp`); the original's `options.cfg` is read once to seed
   it and never written. Server options go in `server.cfg` after the seed line (`src/server/main.cpp`).
8. **Measure first, then change.** Every change comes with a before/after measurement (skill `measure-performance`);
   pure-drawing changes also with a pixel comparison. If the gain is not shown, it does not go in.

## Branches

| Branch | Content |
| --- | --- |
| `mod/alpha-linux-native` (GitHub default) | mod branch + native Linux port (`alpha/platform/linux/`, `cmake/Linux.cmake`, `build.sh`) |
| `mod/alpha-renderdistance-200` | the mod: all shared game code; builds for Windows (MinGW/MSVC) |
| `main` | upstream coremaze, never commit |

- Shared game code: one commit that touches nothing under `alpha/platform/linux/`, applied to **both** branches, mod
  branch first, then cherry-picked onto the Linux branch (check with `git cherry-pick --no-commit` in a throwaway
  worktree of the other branch). Linux-only fixes and Linux compiler flags: Linux branch only. Docs/tools: both.
- PRs go against `mod/alpha-linux-native`; say whether the commit also applies to the mod branch.
- Test hooks, profiler zones and test input (`tools/perf/*.h`, patch scripts) are patched into throwaway copies only,
  never into a game commit. Check every commit with `git show --stat`.

## Building

- Linux native: `alpha/platform/linux/build.sh "<your Cube World folder>" <game dir>` (builds DXVK-native and FAudio once,
  then `Cube` and `Server`, copies data into a playable folder). By hand: see `alpha/platform/linux/README.md`.
- Windows 32 bit (MinGW): `cmake -S alpha -B alpha/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake && cmake --build alpha/build`
- Windows 64 bit: same with `-B alpha/build64 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake -DCUBE_SCRIPTING=OFF`.
- Never point MinGW and Linux builds at the same FreeImage source (`FETCHCONTENT_SOURCE_DIR_FREEIMAGE_SOURCE`):
  `cmake/FreeImage.cmake` patches the source in place for WIN32 and all Linux builds then fail (`_aligned_malloc`).
- No `-march=native` or LTO in anything published (LTO changes the generated world, see lessons).
- Windows test runs under Wine: skill `wine-build-test`.

## Commits

Conventional Commits, scope `alpha`: `perf(alpha):` (pure optimisation, no gameplay change), `feat(alpha):` (new
function/option), `fix(alpha):`, `revert(alpha):`, `build(alpha):`, `docs:`. One topic per commit, English, the body says
what and why, the measurement (before → after, scene, runs) and the source (report, upstream mod, original disassembly).

## Where things are (`alpha/`)

| Area | Place |
| --- | --- |
| Main loop | `platform/linux/main.cpp` (Linux), `src/main.cpp` (Windows): update → render → Present |
| Drawing | `GameController::render()` in `src/cube/GameControllerRender.cpp`; drawing thread: `src/cube/DeferredDevice.cpp` (records D3D9 calls, replays them on a second thread) |
| Chunks, map, zones | `GameController.cpp`: chunk thread (`m_chunkThread`), map thread (`m_mapThread`), zone generation thread; locks `World::enterZones/enterRegions` (`World.cpp`), `WorldMap::loadZone` (`WorldMap.cpp`); generation `WorldGeneration.cpp`, `Zone.cpp` |
| World update, AI, path search | `World::update` (`WorldUpdate.cpp`); `World::beginPathSearch/continuePathSearch/followPath` (`WorldAI.cpp`) |
| Network, client | `GameController.cpp`: `m_receiveThread` (creature packets, `PacketEntitiesEnd`), `m_sendThread` |
| Network, server | `src/cube/Server.cpp` (`Server::start`: accept, per-player receive and send threads, tick), `src/server/main.cpp` (main loop, `server.cfg`) |
| Options | `src/cube/Options.cpp`, menu `src/cube/widgets/OptionsWidget.cpp` (page "Performance") |
| Spike log | `src/cube/SpikeLog.cpp` (option "Spike Log": long frames with cause into `spikes.log`) |
| Linux platform | `platform/linux/` (SDL3, DXVK-native, FAudio, `Rand.cpp`) |

## Expensive lessons

Kept here only if a miss costs a session or silently breaks rule 1; measuring and network details are in the skills.

- **`rand()` order is part of the world.** GCC evaluates several `rand()` in one expression in a different order than
  MSVC: tree colours, villages and loot differed. Multi-`rand()` expressions are split into temporaries in the original's
  order. Never add, remove or reorder `rand()` calls, also not in `render()` (bird/cricket sounds and leaf particles
  draw from the same generator as the game logic).
- **Linux must reproduce Microsoft's `rand`.** glibc's generator differs and is shared across threads: Linux made other
  worlds than Windows. `platform/linux/Rand.cpp` is the MS CRT generator, one state per thread (`--wrap`).
- **LTO and compiler flags rejected:** ±0.4 % fps, and LTO changes all generated zones (float). `WORLDHASH=1` catches that.
- **Do not take the focus reset for a game stutter.** Every focus gain and resize resets the D3D device (original
  behaviour); the reset waits up to ~0.5 s on `m_mapUpdateLock`. A spike after touching the window is this. Rapid resizing
  can crash in DXVK after the reset: old bug, also in the base version.
- **A/B pictures only in the same process**, game time stopped, old/new/old/new (`run.sh ABSHOTS=1`). Separate runs are
  never pixel-identical (zone threads, menus run in real time). Whole-run behaviour comparisons failed for the same reason;
  for logic changes use a shadow check (old and new code on the same state in the same frame, `rand()` replayed).
- **The original Server.exe sends every creature hit on a player twice** (`applyHit` re-queues it): double damage and
  block cost online. Fixed in our server; our client on an original server still gets both.
- **Rubberbanding at 400 %** is the client's receive thread (~8–10 ticks/s at ~300 creatures, two world-lock cycles per
  creature), not the server: the TCP window fills, server `send()` blocks for seconds, data arrives up to ~15 s late.
- **Never push with `--force`.** History was rewritten once (anonymised, 2026-11-08); since then both branches only
  move forward. A rejected push means fetch and rebase your commits, not overwrite.
- **Rejected after measuring:** creature batching and plant instancing (≤ 0.15 ms), merging locks in the drawing thread
  (changes loading), a restart-only drawing-thread switch, single player through a local server (no gain in spikes),
  faster chunk meshing (reverted: 2.5 s faster loading not worth 125 lines in shading), generating several zones at once
  (reverted until the chunk thread's pause was fixed, then brought back). Check `git log` for a revert before retrying.
- **Mass-combat overload is test-made:** above ~60 enemies at one spot `World::update` explodes (projectiles × creatures).
  The mass-combat scene keeps enemy count fixed; do not optimise for a pile no player builds.
