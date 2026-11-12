#!/bin/bash
# both.sh MP orig|lin SEED LABEL: the original Cube.exe (Wine, invisible in gamescope) and our Linux test client on the
# same server (orig = original Server.exe under Wine, lin = our Linux Server), an observer client logs what it sees.
# MP = work folder from setup.sh. Results in MP/res-LABEL. One run at a time (flock on $CW_LOCK).
MP=$1; WH=$2; SEED=$3; L=$4; SECS=130
HERE=$(cd "$(dirname "$0")" && pwd); W=$HERE/../perf; X=$W; MP=$(cd "$MP" && pwd); . "$MP/scene.env"
LOCK=${CW_LOCK:-${XDG_RUNTIME_DIR:-/tmp}/infinite-cubes-measure.lock}
exec 9>"$LOCK"; echo "== waiting for $LOCK $(date +%T)"; flock -w 3000 9 || { echo "lock busy"; exit 1; }
for p in $(pgrep -x Cube); do case "$(readlink /proc/$p/cwd)" in "$MP"/*|/tmp/*) ;; *) echo "a Cube outside the test folders is running (someone plays)"; exit 1;; esac; done
XA=${XAUTHORITY:-$(ls -t /run/user/$(id -u)/xauth_* 2>/dev/null | head -n 1)}
echo "== start $L $WH $(date +%T)"; R=$MP/res-$L; rm -rf $R; mkdir -p $R
S=$MP/$WH-srv; rm -rf $S/Save/*; echo $SEED > $S/server.cfg
if [ $WH = orig ]; then ( cd $S; . $MP/env.sh; ( sleep 290; echo q ) | timeout -k 5 310 nice -n 5 wine Server.exe > $R/server.log 2>&1 ) & sv=$!
else ( cd $S; ( sleep 290; echo q ) | LD_LIBRARY_PATH=./lib timeout -k 5 310 nice -n 5 ./Server > $R/server.log 2>&1 ) & sv=$!; fi
sleep 6
T=$MP/orig-cli; cd $T; rm -rf Save; python3 -I $W/prep_scene.py $MP/orig-saves $T/Save "$WORLD" "$SLOT" 41400000 > /dev/null
( . $MP/env.sh; exec timeout -k 8 280 gamescope --backend headless -W 2560 -H 1440 -w 2560 -h 1440 -- wine Cube.exe ) > $R/cube.log 2>&1 & g=$!
H() { ( . $MP/env.sh; cd $T; timeout 25 wine hand.exe "$@" 2>/dev/null ); }
C() { XAUTHORITY=$XA XT_DISPLAY=$DSP XT_OY=${OY:-0} python3 $HERE/xt.py "$@"; }
sleep 3; DSP=$(grep -oE "Starting Xwayland on :[0-9]+" $R/cube.log | head -1 | grep -oE ":[0-9]+"); echo "Anzeige $DSP"
shot() { GAMESCOPE_WAYLAND_DISPLAY=gamescope-0 timeout 8 gamescopectl screenshot $R/$1.png > /dev/null 2>&1; sleep 0.7; }
sleep 18; H wait; shot o0-start
C where; C click 1280 781; sleep 3; shot o1; C click 1280 595; sleep 2; C click 1460 647; sleep 3; shot o2-welten
C click 2390 1358; sleep 2.5; C click 1280 624; sleep 1; C type 127.0.0.1; sleep 1; shot o3-adresse; C click 1280 707; sleep 6; shot o4-verbunden
grep -E "joined|Client version|Wrong" $R/server.log | cut -c1-80
# Linux-Test-Client
LT=$MP/lin-cli; cd $LT; export TESTDIR=$LT; rm -f Save/map_online_*.db
CUBE_TEST_PIXELS=2560x1440 CUBE_TEST_FIFO=$LT/fifo env -u GLIBC_TUNABLES timeout 220 ./Cube > $R/cli.log 2>&1 & p=$!
for i in $(seq 40); do grep -q 'read cursor.plx' $R/cli.log && break; sleep 1; done; sleep 4
$X/waitscreen start 20; $X/fclick 1280 794 2; $X/waitscreen characters 8; $X/fclick 1280 608 1.5; $X/fclick 1460 660 2; $X/waitscreen worlds 8; sleep 1
$X/fclick 2390 1384 2; $X/fclick 1280 637 1; $X/t text 127.0.0.1; sleep 1; $X/fclick 1280 720 3
$X/waitscreen game 12 && echo "Linux-Client im Spiel $(date +%T)" || echo "Linux-Client NICHT im Spiel"
python3 -I $HERE/observer.py $R/beob.json 115 > $R/beob.txt 2>&1 & bo=$!
sleep 12; $X/t shot $R/l1.ppm; shot o5-spiel
# Cube.exe läuft und greift an, Linux-Client jagt Gegner
C hold w 4000; C mouse 1 1500; C mouse 3 300; shot o6; $X/t chase 1; $X/t god 1
for k in 1 2 3 4 5 6 7 8; do C hold w 1500; C mouse 1 1500; C mouse 3 300; $X/t down 1; sleep 1.5; $X/t up 1; $X/t down 3; sleep 0.3; $X/t up 3; done
shot o7-ende; $X/t shot $R/l2.ppm; sleep 1
cp -a $T/Save/map_online_$SEED.db $R/map-orig-client.db 2>/dev/null
echo "== Cube.exe wird hart beendet $(date +%T)"; kill -TERM $g 2>/dev/null; sleep 12
grep -E "joined|left|disconnect|Connection|Wrong" $R/server.log | tail -6 | cut -c1-80
$X/t shot $R/l3.ppm; sleep 2
wait $bo; kill -TERM $p 2>/dev/null; wait $p; echo "Linux-Client rc=$?"
cat $R/beob.txt | cut -c1-240 | head -24; grep -E "^COMBAT" $R/cli.log | tail -1 | cut -c1-150
cp -a $LT/Save/map_online_$SEED.db $R/map-lin-client.db 2>/dev/null; ls -la $T/Save $R/*.db 2>/dev/null | grep -E "online|map-" | cut -c25-100
wait $sv; tail -6 $R/server.log | cut -c1-90; echo "== ende $(date +%T)"
