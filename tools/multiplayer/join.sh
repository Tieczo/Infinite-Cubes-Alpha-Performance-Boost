#!/bin/bash
# join.sh MP orig|lin SEED LABEL [SECONDS]: our Linux test client on a server (orig = original Server.exe under Wine,
# lin = our Linux Server), an observer client logs what it sees. MP = work folder from setup.sh. Results in MP/res-LABEL.
MP=$1; WH=$2; SEED=$3; L=$4; SECS=${5:-120}
HERE=$(cd "$(dirname "$0")" && pwd); W=$HERE/../perf; X=$W; MP=$(cd "$MP" && pwd); . "$MP/scene.env"
LOCK=${CW_LOCK:-${XDG_RUNTIME_DIR:-/tmp}/infinite-cubes-measure.lock}
exec 9>"$LOCK"; echo "== waiting for $LOCK $(date +%T)"; flock -w 3000 9 || { echo "lock busy"; exit 1; }
for p in $(pgrep -x Cube); do case "$(readlink /proc/$p/cwd)" in "$MP"/*|/tmp/*) ;; *) echo "a Cube outside the test folders is running (someone plays)"; exit 1;; esac; done
echo "== start $L $(date +%T)"
T=$MP/lin-cli; R=$MP/res-$L; rm -rf $R; mkdir -p $R
S=$MP/$WH-srv; rm -rf $S/Save/*; echo $SEED > $S/server.cfg; :
cd $S
if [ $WH = orig ]; then . $MP/env.sh; ( sleep $((SECS+60)); echo q ) | timeout -k 5 $((SECS+80)) nice -n 5 wine Server.exe > $R/server.log 2>&1 & sv=$!
else ( sleep $((SECS+60)); echo q ) | LD_LIBRARY_PATH=./lib timeout -k 5 $((SECS+80)) nice -n 5 ./Server > $R/server.log 2>&1 & sv=$!; fi
unset WINEPREFIX WINEARCH WINEDEBUG WINEDLLOVERRIDES
sleep 6; cd $T; export TESTDIR=$T; rm -f Save/map_online_*.db
CUBE_TEST_PIXELS=2560x1440 CUBE_TEST_FIFO=$T/fifo env -u GLIBC_TUNABLES timeout $((SECS+55)) ./Cube > $R/cli.log 2>&1 & p=$!
for i in $(seq 40); do grep -q 'read cursor.plx' $R/cli.log && break; sleep 1; done; sleep 4
$X/waitscreen start 20; $X/fclick 1280 794 2; $X/waitscreen characters 8; $X/fclick 1280 608 1.5; $X/fclick 1460 660 2; $X/waitscreen worlds 8; sleep 1
$X/fclick 2390 1384 2; $X/t shot $R/a-connect.ppm; sleep 1.5
$X/fclick 1280 637 1; $X/t text 127.0.0.1; sleep 1; $X/t shot $R/b-text.ppm; sleep 1.5
$X/fclick ${CX:-1280} ${CY:-720} 3; $X/t shot $R/c-after.ppm; sleep 2
$X/waitscreen game 12 && echo "Client im Spiel $(date +%T)" || echo "Client NICHT im Spiel"
python3 -I $HERE/observer.py $R/beob.json $((SECS-5)) > $R/beob.txt 2>&1 & bo=$!
sleep 15; $X/t shot $R/d-game.ppm; sleep 2; $X/t chase 1; $X/t god 1
end=$(( $(date +%s) + SECS - 30 )); while [ $(date +%s) -lt $end ]; do $X/t down 1; sleep 1.5; $X/t up 1; sleep 0.1; $X/t down 3; sleep 0.3; $X/t up 3; sleep 0.1; done
$X/t worldhash 3; sleep 3; $X/t shot $R/e-end.ppm; sleep 2
wait $bo; kill -TERM $p 2>/dev/null; wait $p; echo "Client rc=$?"; sleep 2
cat $R/beob.txt | cut -c1-220; grep -E "^COMBAT" $R/cli.log | tail -2 | cut -c1-150; echo "Serverlog:"; grep -v "Waiting" $R/server.log | tail -8 | cut -c1-100
cp -a $T/Save/map_online_$SEED.db $R/ 2>/dev/null; ls -la $T/Save | grep -E "online" | cut -c25-90
wait $sv; echo "Server beendet"; echo "== ende $(date +%T)"
