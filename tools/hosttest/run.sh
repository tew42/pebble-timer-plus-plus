#!/bin/bash
# Build and run the whole host harness. Not part of the app: it compiles the real settings.c,
# timer.c, animation.c and text_render.c against stub/pebble.h so the logic can be exercised
# without an ARM toolchain.
set -u
ROOT=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
# the compiler flags and the test binaries are all relative, so work from this directory
cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
R=$ROOT/src/c
CF="-std=c99 -Wall -Wextra -Wno-unused-parameter -I. -Istub -I$R"
fail=0
node gen_configopts.js "$ROOT" >/dev/null || fail=1
# Each binary is removed before it is rebuilt. Without that a build failure leaves the last good
# one in place and the run loop below happily reports "ok" for code which no longer compiles.
for t in test ringtest convention flicker shadetest timertest clocktest controltest settingstest; do
  rm -f "$t"
  gcc $CF -o "$t" "$t.c" stub/stub.c || { echo "BUILD FAIL $t"; fail=1; continue; }
done
# rotarytest includes rotary_kit.c, and needs libm for the arcs it draws to drive it with
rm -f rotarytest
gcc $CF -o rotarytest rotarytest.c stub/stub.c -lm || { echo "BUILD FAIL rotarytest"; fail=1; }
rm -f glyphtest
gcc $CF -DNDEBUG=1 -o glyphtest glyphtest.c stub/stub.c "$R/text_render.c" || { echo "BUILD FAIL glyphtest"; fail=1; }
rm -f layouttest
gcc $CF -DNDEBUG=1 -o layouttest layouttest.c stub/stub.c "$R/text_render.c" || { echo "BUILD FAIL layouttest"; fail=1; }
rm -f anitest
gcc $CF -fsanitize=address -g -o anitest anitest.c stub/stub.c || { echo "BUILD FAIL anitest"; fail=1; }
for t in test ringtest convention flicker shadetest timertest clocktest controltest settingstest rotarytest glyphtest layouttest anitest; do
  if ./"$t" >/tmp/$t.log 2>&1; then echo "  ok   $t"; else echo "  FAIL $t"; tail -20 /tmp/$t.log; fail=1; fi
done
if node jstest.js >/tmp/jstest.log 2>&1; then echo "  ok   jstest"; else echo "  FAIL jstest"; cat /tmp/jstest.log; fail=1; fi
# main.c and drawing.c cannot be run here, but they can be compiled: syntax only, every platform
bash syntax.sh "$ROOT" || fail=1
[ $fail -eq 0 ] && echo "harness green" || echo "HARNESS FAILURES"
exit $fail
