---
name: measure-performance
description: Measure fps and frame times of Infinite Cubes before and after a change (scenes normal/fight/mass fight, headless, lock, alternating runs, spread, pixel comparison). Use for any perf(alpha) change, any claim of "faster" or "no regression", and before a PR that touches render, world update, chunks or AI.
---

# Measuring performance

Scripts: `tools/perf/` (see `tools/README.md` for the environment variables). Linux native builds only.

1. **Builds.** `tools/perf/build-measure.sh <base-commit> $PERF_TMP/old` and `... <your-commit> $PERF_TMP/new`.
   Build with `nice`, at most `-j8`; no large build while someone else holds the lock (`flock -n "$CW_LOCK" true`).
2. **Runs.** Always through `run.sh` (it takes `$CW_LOCK`: one measuring run on the whole machine; it refuses to run
   while someone plays). Headless in gamescope is the default; compare headless only with headless.
   - Settings fixed by the script: 2560x1440 back buffer, 8x AA, no fps limit, 11:30, camera spin 36°/s.
   - Scenes at 400/400/100 %: **normal** (170 s, measure from 110), **fight** (`COMBAT=1 GOD=1 CHASE=1`, scene `kampf`),
     **mass fight** (`MOB=30 ... CHASE=1`, 150 s). Also 200 % (100 s, from 50) when drawing changes: there the GPU is the
     limit and it must not get worse. If GPU-bound (time in Present), add `AA=2` as CPU comparison.
3. **Alternate:** old-1, new-1, old-2, new-2, old-3, new-3 (at least 2, better 3 per variant). Labels `name-N`.
4. **Evaluate:** `tools/perf/summarize.py $PERF_TMP/logs/*.log` → mean and range of fps, p99, max frame.
   - A gain counts only if the ranges do not overlap. Typical spread: ±0.5–1 % normal, ±2 % fight.
   - **Regression** = lower mean outside the spread, higher p99/p99.9, more frames over 16/33 ms, or a new max-frame
     cause in any scene, also at 200 %. A death/loot frame of 33–45 ms in fights is old.
   - Where the time goes: `symbolize.py LABEL.samples BIN`; long frames: option "Spike Log" (`spikes.log`).
5. **Same picture** (drawing changes): A/B in one process, time stopped: build with `AB_BASE=<base>` after listing your
   code regions in `ab_patch.py`, run with `ABSHOTS=1`, then `ab_compare.py $PERF_TMP/logs/LABEL-ab`. Required:
   `PIXELGLEICH` in day and night (stars), plus the control picture (`leer`) must differ, else the region is not on screen.
6. **Same behaviour** (logic changes): `WORLDHASH=1` + `worldhash_compare.py` for generation; a shadow check
   (`shadow_check_example.py`) for AI/update code. Whole-run comparisons do not work (threads, real-time menus).
7. **Report** in the commit body: scene, settings, runs, mean (range), p99, before → after. Say what was not measured.

Pitfalls:
- Do not touch a test window during a run (focus gain resets the device and makes a spike). Values across the
  `rand()`-order fix (tree colours) and from old maximised-window runs are not comparable.
- Minimised measures like visible, headless roughly too. Headless gamescope is the default so nobody mistakes a test
  window for their game (a fullscreen test run once was).
- Stars over stunned creatures turn by the wall clock, not game time: freeze that too for A/B pictures.
- Proof against the original: maps bit-identical with `map_online_<seed>.db` written by Cube.exe. Tiles saved in 2015
  differ for today's Cube.exe too; that is not a port bug.
- Tool anchors drift: the profiler patch anchors on source lines (`timeGetTime()` became `gameTime()`). Fix the tool,
  never bend game code to a tool.
