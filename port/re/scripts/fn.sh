#!/bin/sh
# print decompiled functions without blank lines and local declarations: fn.sh addr...
D=$(dirname "$0")/../decomp/all_marked.c
for a in "$@"; do
awk -v a="$a" '$0 ~ "^// ===== FUN_00"a {p=1;print;next} /^\/\/ ===== /{p=0} p' "$D" | grep -v '^\s*$' |
  grep -vE '^  (undefined[0-9]?|int|uint|float|char|byte|bool|short|ushort|DWORD|double|code|BOOL|HANDLE|LPCSTR|longlong|float10|LPRECT|HDC|CHAR|ulonglong|MMRESULT|HGLOBAL|void) \**[A-Za-z_0-9]+( \[[0-9]+\])?;$' |
  grep -v 'WARNING: Globals starting'
done
