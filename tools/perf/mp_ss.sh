#!/bin/bash
# Capture both TCP queues and rwnd_limited on port 12345 every 200 ms.
set -eu
while :; do
    printf 'TIME %s\n' "$(date +%s.%N)"
    ss -tni '( sport = :12345 or dport = :12345 )'
    sleep .2
done
