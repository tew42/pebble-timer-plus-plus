#!/bin/bash
# Record the README sequence on each platform named, one emulator at a time, with the same settings.
#   run_all.sh aplite basalt chalk emery gabbro
# PROJECT is the app to install, normally the throwaway copy with epoch_guard.patch applied (see
# README.md); OUT is where the recordings go, one directory per platform; PY is pebble-tool's python.
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT=${PROJECT:?set PROJECT to the patched copy of the app}
OUT=$(mkdir -p "${OUT:-recordings}" && cd "${OUT:-recordings}" && pwd)
PY=${PY:-$HOME/.local/share/uv/tools/pebble-tool/bin/python}
cd "$PROJECT" || exit 1
for p in "$@"; do
  pebble kill >/dev/null 2>&1; sleep 2
  for try in 1 2 3; do
    timeout 240 pebble install --emulator $p >/dev/null 2>&1 && break
    sleep 5
  done
  sleep 3
  # tenSecondUpdatesAbove 40 s, minuteUpdatesAbove 1 min, timerColor green, chronoColor red,
  # instantStart off, overrunChrono on (keys from build/js/message_keys.json)
  timeout 60 pebble send-app-message --emulator $p --app-uuid c1a8292f-bf99-44ee-888c-517a7f2a48f1 \
    --int 10000=40 10001=1 10002=65280 10003=16711680 10005=255 10006=1 >/dev/null 2>&1
  sleep 2
  rm -rf "$OUT/$p"
  timeout 200 "$PY" "$HERE/capture.py" $p "$OUT/$p" 2>&1 | tail -1
done
pebble kill >/dev/null 2>&1
