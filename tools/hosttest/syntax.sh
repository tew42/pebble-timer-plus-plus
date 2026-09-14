#!/bin/bash
# Syntax-check the two files the rest of the harness cannot compile. main.c and drawing.c pull in
# the whole Pebble API, so they were only ever checked by the SDK build -- which is how a missing
# struct member and a call above its declaration got as far as a push. stub/syntax holds
# declarations for what they use, and every platform variant they branch on gets a pass, since the
# one bit and the round cases take different code, and PBL_TOUCH decides whether the click
# wheel is compiled in at all.
set -u
R=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}/src/c
cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
CF="-std=c99 -Wall -Wextra -Werror -Wno-unused-parameter -Istub/syntax -I. -Istub -I$R"
fail=0
check() { # name, then defines
  local name=$1; shift
  for f in main.c drawing.c; do
    if ! gcc $CF "$@" -fsyntax-only "$R/$f" 2>/tmp/syntax_${name}_$f.log; then
      echo "  FAIL $name: $f"; sed -n '1,12p' /tmp/syntax_${name}_$f.log; fail=1
    fi
  done
  [ $fail -eq 0 ] && echo "  ok   $name"
}
check emery   -DPBL_PLATFORM_EMERY -DPBL_TOUCH -DPBL_COLOR -DPBL_RECT -DPBL_DISPLAY_WIDTH=200 -DPBL_DISPLAY_HEIGHT=228
check basalt  -DPBL_PLATFORM_BASALT -DPBL_COLOR -DPBL_RECT -DPBL_DISPLAY_WIDTH=144 -DPBL_DISPLAY_HEIGHT=168
check chalk   -DPBL_PLATFORM_CHALK -DPBL_COLOR -DPBL_ROUND -DPBL_DISPLAY_WIDTH=180 -DPBL_DISPLAY_HEIGHT=180
check gabbro  -DPBL_PLATFORM_GABBRO -DPBL_TOUCH -DPBL_COLOR -DPBL_ROUND -DPBL_DISPLAY_WIDTH=260 -DPBL_DISPLAY_HEIGHT=260
check aplite  -DPBL_PLATFORM_APLITE -DPBL_BW -DPBL_RECT -DPBL_DISPLAY_WIDTH=144 -DPBL_DISPLAY_HEIGHT=168
check diorite -DPBL_PLATFORM_DIORITE -DPBL_BW -DPBL_RECT -DPBL_DISPLAY_WIDTH=144 -DPBL_DISPLAY_HEIGHT=168
check flint   -DPBL_PLATFORM_FLINT -DPBL_BW -DPBL_RECT -DPBL_DISPLAY_WIDTH=144 -DPBL_DISPLAY_HEIGHT=168
exit $fail
