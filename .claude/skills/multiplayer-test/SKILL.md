---
name: multiplayer-test
description: Check that Infinite Cubes still plays with the original Cube World Alpha in both directions (our client on the original Server.exe, the original Cube.exe on our server). Use after any change to network, server, packets, hits, chat or world generation, and before a release.
---

# Multiplayer test in both directions

Scripts: `tools/multiplayer/` (environment in `tools/README.md`). Copies only: never run in your Cube World folder or
your playing Wine prefix.

1. `tools/multiplayer/setup.sh $MP kampf` once (needs `CW_ORIGINAL_DIR`, `CW_GAME_DIR`, `CW_SCENES_DIR`, `MEASURE_BIN`).
   After a rebuild copy the new `Server` into `$MP/lin-srv` and the new test `Cube` into `$MP/lin-cli`.
2. Both directions, same seed:
   - `both.sh $MP lin <seed> ours` : original Cube.exe + our Linux client on **our** server.
   - `both.sh $MP orig <seed> orig` : the same on the original **Server.exe** (Wine).
   - quicker: `join.sh $MP orig|lin <seed> LABEL 120` (our client only).
3. Pass when, in both directions: all clients join ("Client version: 3, Player N joined"), the observer sees every player
   (`res-LABEL/beob.txt`), fights work (kills), the map tiles of both clients are identical
   (`map_shape.py res-LABEL/map-orig-client.db res-LABEL/map-lin-client.db`), `chat_check.py` passes against our server,
   and the server survives a client that is killed hard.
4. Hits: on our server no hit arrives twice (observer: `hits_doubled` 0); on the original Server.exe each hit arrives twice,
   that is the original's bug, not a regression.
5. For network performance (rubberbanding) measure ticks per second at the client and `ss -tni` (Send-Q, Recv-Q,
   `rwnd_limited`) with ~300 creatures (400 %) and at 100 %; compare with the original Server.exe at 100 %.
6. Server objects equal to Server.exe's. Keep the tick quirk: `lastTick` is set before `Sleep` and shared, so ~100
   instead of 50 ticks/s (original). Clients delete every creature not sent in a tick, so far creatures cannot be sent
   less often.

Rules: protocol, packet layout and world generation stay unchanged. Server behaviour that changes what players see is a
`server.cfg` line with the original's value as default. Port 12345 is shared: one test at a time (`CW_LOCK`).
