#!/bin/sh
# list "file function count" for compile errors across generated files
cd "$(dirname "$0")/.."
cmake --build build-pc --target kok -j8 -- -k 2>&1 | grep -E "gen/g_[0-9a-f]+\.c:[0-9]+:[0-9]+: error" | while IFS=: read -r f l c rest; do
  fn=$(awk -v L="$l" 'NR<=L && /^\/\* ---- FUN_/ {x=$3} END{print x}' "$f"); echo "$(basename $f) $fn"; done | sort | uniq -c
