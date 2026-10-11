#!/bin/sh
# Regenerate src/game/gen from the decompile and apply the mechanical post-fixes.
set -e
cd "$(dirname "$0")/.."
P=tools/.venv/bin/python
$P tools/gen_game.py
$P tools/rmfn.py src/game/gen/g_400000.c 401000 4011a0 401220 4014e0 401650 401680 401710 401780 401800 4019a0 401a20 401b20 401b50 401b80 401bb0 401cb0 401d50 401dc0 401e20 401e50 401ec0 401f70 4021c0 402390 4023f0 4024a0 402630 4026d0 402740 4029e0 402b50 402e70 402ea0 402f80
$P tools/rmfn.py src/game/gen/g_440000.c 440f00 440d90 440cf0 4408f0 440070
$P tools/rmfn.py src/game/gen/g_404000.c 4052a0 405a40 405f70 407790 407360
$P tools/rmfn.py src/game/gen/g_43c000.c 43e810 43ec30
$P tools/rmfn.py src/game/gen/g_424000.c 424130 424460 426f50
$P tools/rmfn.py src/game/gen/g_428000.c 42a730 42a900 4282b0
$P tools/rmfn.py src/game/gen/g_42c000.c 42c7e0
$P tools/rmfn.py src/game/gen/g_434000.c 437f80 4378d0 437980 435340
$P tools/rmfn.py src/game/gen/g_438000.c 4380b0 438200 4390c0 438f80 438540 4387d0 438af0 438c70 438e00
$P tools/rmfn.py src/game/gen/g_430000.c 4324b0 432530 430110 4326c0 432a30
$P tools/rmfn.py src/game/gen/g_43c000.c 43ddb0 43dee0 43e120 43e290
# music calls: repeat counts lost by Ghidra (see disassembly of each call site)
sed -i -E 's/f_401dc0\(([^()]*)\);/f_401dc0(\1, 1000);/; s/f_401d50\(5\);/f_401d50(5, 10000);/; s/f_401d50\(2\);/f_401d50(2, 1);/; s/f_401d50\(1\);/f_401d50(1, 1000);/' src/game/gen/g_4*.c
sed -i -E "s/^uint32_t f_407790\(void\);/uint32_t f_407790(uint32_t file, uint32_t out);/" src/game/gen/protos.h
sed -i -E 's/^uint32_t f_405f70\(void\);/uint32_t f_405f70(uint32_t file, uint32_t out, uint32_t ox_bits, uint32_t oy_bits, uint32_t oz_bits);/' src/game/gen/protos.h
sed -i -E 's/^void f_401d50\(int32_t param_1\);/void f_401d50(int32_t param_1, uint32_t repeats);/; s/^void f_401dc0\(int32_t param_1\);/void f_401dc0(int32_t param_1, uint32_t repeats);/' src/game/gen/protos.h
sed -i -E "s/^void f_42a730\(/uint32_t f_42a730(/" src/game/gen/protos.h
sed -i -E "s/^void f_42c7e0\(.*\);/void f_42c7e0(int32_t player, int32_t page);/" src/game/gen/protos.h
$P tools/patches.py
# 60 / fps and friends are 32-bit idiv in the original; Ghidra's int64 divisor costs a __divdi3 call
# (soft 64-bit division on the PSP) at ~350 sites. After patches.py, whose patterns use Ghidra's text.
sed -i -E 's#/ \(int64_t\)\(#/ (int32_t)(#g' src/game/gen/g_4*.c
# void functions whose result callers read (garbage EAX in the original): return uint32_t. The list
# was found with "fix_voidret.py auto" (compiler diagnostics of a configured build-pc); it is fixed
# here so a fresh checkout generates the same code.
$P tools/fix_voidret.py 401800 4029e0 4032c0 406210 407360 410f50 4244c0 426810 427a80 4294e0 \
    42a860 42aef0 42af20 42b190 4378d0 438520 43f380
