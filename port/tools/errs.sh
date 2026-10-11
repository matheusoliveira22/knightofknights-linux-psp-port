#!/bin/sh
# errors of one generated file, grouped by function: tools/errs.sh g_400000
F=src/game/gen/$1.c
cmake --build build-pc --target kok 2>&1 | grep "$1.c:[0-9]*:[0-9]*: error" | while IFS=: read -r f l c rest; do
  fn=$(awk -v L="$l" 'NR<=L && /^\/\* ---- FUN_/ {x=$3} END{print x}' "$F"); echo "$fn:$l:$rest"; done
