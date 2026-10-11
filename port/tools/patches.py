"""Content-matched fixes applied to the regenerated src/game/gen files (run by postgen.sh).
Each patch: (file, regex, replacement, expected match count)."""
import re, sys

PATCHES = [
    # 0x40a730: material reset (pushes: struct at _fr-0x80, this at _fr-0x84)
    ("g_408000.c", r"TODO_COM_c\(\);", "COM_Material2_SetMaterial(MU32(_fr - 0x84), MU32(_fr - 0x80));", 2),
    # 0x40c800: SetPosition(frame, ref, x, y, z) and camera GetPosition(ref, &v) lost their pushes
    ("g_40c000.c", r"TODO_COM_f4\((MU32\(\(0x4a3c98 \+ \(uint32_t\)\(\(param_1 \* 0x2e\)\) \* 4\)\)), (MU32\(\(0x4a3c9c \+ \(uint32_t\)\(\(param_1 \* 0x2e\)\) \* 4\)\)), (MU32\(\(0x4a3cbc \+ \(uint32_t\)\(iVar11\)\)\))\);",
     r"COM_Frame2_SetPosition(\1, \2, \3, MU32((0x4a3cc0 + (uint32_t)(iVar11))), MU32((0x4a3cc4 + (uint32_t)(iVar11))));", 1),
    ("g_40c000.c", r"COM_Frame2_GetPosition\(MU32\(0x4a3898\)\);",
     "COM_Frame2_GetPosition(MU32(0x4a3898), MU32((0x4a3c98 + (uint32_t)((param_1 * 0x2e)) * 4)), (_fr - 0x5c));", 1),
    ("g_40c000.c", r"TODO_COM_f4\(piVar4, (MU32\(\(0x4a3c98 \+ \(uint32_t\)\(\(param_1 \* 0x2e\)\) \* 4\)\)), MU32\(\(\(MU32\(\(0x4a3ca0 \+ \(uint32_t\)\(\(param_1 \* 0x2e\)\) \* 4\)\) \+ iVar12\) \+ \(uint32_t\)\(10\) \* 4\)\)\);",
     r"COM_Frame2_SetPosition(piVar4, \1, MU32(MU32(0x4a3ca0 + param_1 * 0xb8) + iVar12 + 0x28), MU32(MU32(0x4a3ca0 + param_1 * 0xb8) + iVar12 + 0x2c), MU32(MU32(0x4a3ca0 + param_1 * 0xb8) + iVar12 + 0x30));", 1),
    # 0x427ed0: SetPosition(frame, scene, x, y + 1, z) - z push lost
    ("g_424000.c", r"TODO_COM_f4\(piVar3, MU32\(0x4a388c\), MU32\(\(param_1 \+ 0xc\)\), \(MF\(\(param_1 \+ 0x10\)\) \+ MF\(0x44d1cc\)\)\);",
     "COM_Frame2_SetPosition(piVar3, MU32(0x4a388c), MU32((param_1 + 0xc)), BITU(MF((param_1 + 0x10)) + MF(0x44d1cc)), MU32((param_1 + 0x14)));", 1),
    # IDirect3DRMMeshBuilder2::Scale(0.6, 0.6, 0.6)
    ("g_428000.c", r"TODO_COM_34\(MU32\(puVar3\), 0x3f19999a, 0x3f19999a, 0x3f19999a\);",
     "COM_MeshBuilder2_Scale(MU32(puVar3), 0x3f19999a, 0x3f19999a, 0x3f19999a);", 1),
    # text routines (0x4326c0, 0x432a30, 0x432de0, 0x435340): the HDC lived in a callee-saved register
    ("g_430000.c", r"w_(SelectObject|SetTextColor|SetBkColor|SetBkMode|DrawTextA|TextOutA)\(unaff_E[A-Z]{2}, ", r"w_\1(HDC_TEXT, ", -1),
    ("g_434000.c", r"w_(SelectObject|SetTextColor|SetBkColor|SetBkMode|DrawTextA|TextOutA)\(unaff_E[A-Z]{2}, ", r"w_\1(HDC_TEXT, ", -1),
    ("g_430000.c", r"ReleaseDC\(([^,()]+(?:\([^()]*\))?), unaff_E[A-Z]{2}\)", r"ReleaseDC(\1, HDC_TEXT)", -1),
    ("g_434000.c", r"ReleaseDC\(([^,()]+(?:\([^()]*\))?), unaff_E[A-Z]{2}\)", r"ReleaseDC(\1, HDC_TEXT)", -1),
    ("g_430000.c", r"\(uint32_t\)\(CONCAT22\(MU16\(_fr - 0x32a\), CONCAT11\(MU8\(_fr - 0x32b\), MU8\(\(_fr - 0x32f\) \+ 3\)\)\)\)", "HDC_TEXT", -1),
    # 0x414e40: SetPosition(sub frame, actor frame, 0, 0, -1) in both branches
    ("g_414000.c", r"TODO_COM_f4\(\);", "COM_Frame2_SetPosition(MU32((param_1 + (uint32_t)(0xd) * 4)), MU32(param_1), 0, 0, 0xbf800000);", 1),
    # 0x440c30: returns the created user visual (local_4) on success, not the callback address
    ("g_440000.c", r"VRETV\(\(~\(-\(\(uint32_t\)\(\(iVar1 != 0\)\)\)\) & \(uint32_t\)pcVar2\)\);",
     "VRETV((~(-((uint32_t)((iVar1 != 0)))) & MU32(_fr - 0x4)));", 1),
    # 0x427030 (enemy AI): nearest-player search starts at 1000.0 (mov [esp+0x18],0x447a0000 among
    # the prologue pushes), and the chosen action handler gets the actor (push ebp = param_1)
    ("g_424000.c", r"\(fVar10 = 0\.0\);", "(fVar10 = 1000.0);", 1),
    ("g_424000.c", r"\(iVar9 = iVar11\);\n(\s+)\(uVar3 = ms_rand\(\)\);", r"(uVar3 = ms_rand());", 1),
    # 0x40ee20 (launched / juggled state 11): the animation time advance is an fstp of the float
    # sum; Ghidra wrapped it in an int conversion, collapsing the time to ~0 so the enemy stayed
    # in its falling animation forever
    ("g_40c000.c", r"\(M32\(\(param_1 \+ \(uint32_t\)\(0x19\) \* 4\)\) = \(int32_t\)\(\(\(\(float\)\(iVar8\) \* MF\(0x44d200\)\) \+ BITF\(M32\(\(param_1 \+ \(uint32_t\)\(0x19\) \* 4\)\)\)\)\)\);",
     "(M32((param_1 + (uint32_t)(0x19) * 4)) = BITU((((float)(iVar8) * MF(0x44d200)) + BITF(M32((param_1 + (uint32_t)(0x19) * 4))))));", 1),
    # 0x429880: actor field +0x58 compared with 11 (Ghidra typed it float; see _ITEM_KIND)
    ("g_428000.c", r"MF\(\((pfVar[67]) \+ \(uint32_t\)\(0x16\) \* 4\)\) == 1\.54143e-44", r"M32((\1 + (uint32_t)(0x16) * 4)) == 11", 2),
    # 0x4056f0: collision mesh of map piece 2
    ("g_404000.c", r"f_405f70\(\)", "f_405f70(0x4a1974, 0x488ca0, 0, 0, 0)", 1),
    # 0x435640 (event scene setup): CreateAnimationSet(rm, &0x488bc4) at 0x43567f; Ghidra passed the
    # stale CreateFrame push slots, writing the set to VA = scene handle and leaving 0x488bc4 null
    ("g_434000.c", r"COM_RM2_CreateAnimationSet\(MU32\(_fr - 0x10c\), MU32\(_fr - 0x108\)\);",
     "COM_RM2_CreateAnimationSet(MU32(0x4a3880), 0x488bc4);", 1),
]

# occurrence-specific patches: (file, literal text, [replacement for 1st, 2nd, ...])
NTH = [
    # 0x424c60 / 0x424e00: collision mesh of a map object; file record +0x14, vector = object position.
    # These run after the FUNC patches, so they must use `slot` (the renamed edi argument) directly.
    ("g_424000.c", "    f_405f70();", [
        "    f_405f70(0x4797cc + MU32(0x4a1a60 + slot * 4) * 0x30, param_1 + 0x3c, "
        "MU32(0x4a1a88 + slot * 4), MU32(0x4a1ab0 + slot * 4), MU32(0x4a1ad8 + slot * 4));",
        "    f_405f70(0x4a2588 + MU32(0x4a1a60 + slot * 4) * 0x60, param_1 + 0x3c, "
        "MU32(0x4a1a88 + slot * 4), MU32(0x4a1ab0 + slot * 4), MU32(0x4a1ad8 + slot * 4));"]),
    ("g_424000.c", "COM_RM2_CreateFrame(MU32(0x4a3880), MU32(0x4a3890));", [
        "COM_RM2_CreateFrame(MU32(0x4a3880), MU32(0x4a3890), param_1);"]),
]

# per-function patches: (file, function address, [(regex, replacement, expected count), ...]),
# applied only inside that function's body
_SPAWN_SLOT = [  # spawn handlers called as fn(actor, slot) through the table at 0x460bd4
    (r"\(uint32_t param_1\)\n", "(uint32_t param_1, int32_t slot)\n", 1),
    (r"    int32_t unaff_E[A-Z]{2} = 0;\n", "", -1),
    (r"unaff_E[A-Z]{2}|\(int32_t\)piVar2|M32\(_fr - 0xc\)", "slot", -1),
    (r"    \(piVar2 = param_1\);\n", "", -1),
]
# AI action handlers: GetPosition(target, self, &v) fills v at _fr-0xc; the decompile read the
# self handle (fVar2) as v.x and an unset register as v.z
_AI_VEC = [
    (r"-\(fVar2\)", "-(MF(_fr - 0xc))", -1),
    (r"\(unaff_ESI < MF\(0x44d1e4\)\) && \(fVar2 < MF\(0x44d240\)\)\) && \(MF\(0x44d8d4\) < fVar2\)",
     "(MF(_fr - 0x4) < MF(0x44d1e4)) && (MF(_fr - 0xc) < MF(0x44d240))) && (MF(0x44d8d4) < MF(_fr - 0xc))", -1),
    (r"    float unaff_ESI = 0;\n", "", -1),
]
# Integer fields Ghidra typed as float: equality against denormal constants (bit patterns 4, 5, 6,
# 11) works on x86 but fails on the PSP, whose FPU flushes denormals to zero (plat_psp fpu_setup).
_ITEM_KIND = [  # 0x427c30 item pickup (R): kind of the nearby item actor (+0x12c), 4/5/6
    (r"    float fVar2 = 0;\n", "    float fVar2 = 0;\n    int32_t iKind = 0;\n", 1),
    (r"\(fVar2 = MF\(\(pfVar9 \+ \(uint32_t\)\(0x46\) \* 4\)\)\)", "(iKind = M32((pfVar9 + (uint32_t)(0x46) * 4)))", 1),
    (r"\(3 < BITU\(fVar2\)\)", "(3 < (uint32_t)iKind)", 1),
    (r"fVar2 == 5\.60519e-45", "iKind == 4", 1),
    (r"fVar2 == 7\.00649e-45", "iKind == 5", 1),
    (r"fVar2 == 8\.40779e-45", "iKind == 6", 1),
]
# Weapon move handlers keep the integer 60/fps in a stack slot Ghidra typed as float, then convert
# it back numerically: (int32_t)(denormal) = 0, so the move's time never advanced and the player
# stayed frozen in the move until something hit them. Keep the slot an integer.
_FPS_SLOT = [
    (r"\(MF\(_fr - 0x4\) = BITF\(\(0x3c / \(int64_t\)\(M32\(0x488f48\)\)\)\)\)",
     "(M32(_fr - 0x4) = (int32_t)(0x3c / (int64_t)(M32(0x488f48))))", 3),
    (r"\(int32_t\)\(MF\(_fr - 0x4\)\)", "M32(_fr - 0x4)", -1),
]
FUNC = [("g_424000.c", "427c30", _ITEM_KIND)] + \
       [("g_418000.c", a, _FPS_SLOT) for a in ("41aca0", "41b590")] + \
       [("g_41c000.c", a, _FPS_SLOT) for a in ("41c680", "41caf0")] + [("g_420000.c", "4232c0", _FPS_SLOT)] + [("g_424000.c", a, _SPAWN_SLOT) for a in ("4248f0", "424b30", "424c60", "424e00", "424fe0", "4250f0", "4251f0")] + \
       [("g_424000.c", a, _AI_VEC) for a in ("4272c0", "427340", "427400", "4274c0", "427580", "427710", "4277e0", "4278b0", "4279c0")] + [
    # player spawn fn(actor, x, y, z): the position args were lost (callers pass (+-1, 10, 0))
    ("g_424000.c", "4252f0", [
        (r"\(uint32_t param_1\)\n", "(uint32_t param_1, uint32_t px, uint32_t py, uint32_t pz)\n", 1),
        (r"    uint32_t unaff_E[A-Z]{2} = 0;\n", "", 3),
        (r"unaff_EDI, unaff_ESI, unaff_EBX", "px, py, pz", 1)]),
    # weapon swap: the actor pointer lives in the (overwritten) argument slot
    ("g_428000.c", "42bd10", [
        (r"    int32_t unaff_ESI = 0;\n", "", 1),
        (r"unaff_ESI", "(0x480340 + param_1 * 0x144)", 5)]),
    # weapon visual cleanup: both indices are the loop counter ([esp+0x10])
    ("g_428000.c", "42b9b0", [
        (r"    int32_t unaff_E(?:BP|DI) = 0;\n", "", 2),
        (r"unaff_E(?:BP|DI)", "M32(_fr - 0x4)", 2)]),
    # ring menu icons: the loop counter shares the argument slot (param_1)
    # port (PSP) speed-up: the map/object collision helpers (plane dot product 0x42a7e0, point-plane
    # distance 0x42a810) run for every face x actor x collision object each tick; in double precision
    # they are soft-float calls on the PSP (no double FPU) and made hedge-filled rooms crawl. The
    # results are stored as floats, so single precision only changes the last bits.
    ("g_428000.c", "42a7e0", [(r"\(double\)", "(float)", 7)]),
    ("g_428000.c", "42a810", [(r"\(double\)", "(float)", 13), (r"ABS\(", "fabsf(", 1), (r"SQRT\(", "sqrtf(", 1)]),
    # room enemy roster (0x4043db: jmp [stage*4 + 0x404b70]): Ghidra read the dword jump table as a
    # byte table and used the jump targets as case labels, so no case ever matched and every stage
    # got the default roster (wrong enemy kinds for the random spawns). Table: stages 0,2 -> 0x4043e2,
    # 1 -> 0x404433, 3,5 -> 0x404496, 4,6 -> 0x404503.
    ("g_400000.c", "403c70", [
        (r"switch \(MU8\(\((?:0x404b70|TODO_addr_of) \+ \(uint32_t\)\((MU?32\(\(\(_fr - 0x144\) \+ \(uint32_t\)\(0x50\) \* 4\)\))\)\)\)\)", r"switch (\1)", 1),
        (r"case 0x4043e2:", "case 0: case 2:", 1),
        (r"case 0x404433:", "case 1:", 1),
        (r"case 0x404496:", "case 3: case 5:", 1),
        (r"case 0x404503:", "case 4: case 6:", 1)]),
    # port feature: on single-pad platforms (PSP) the mode select cursor skips the 2P modes and
    # shows them greyed out (sys.c game_menu_step / game_menu_decorate)
    ("g_430000.c", "4310b0", [
        (r"\(M32\(0x4a3820\) = \(M32\(0x4a3820\) \+ -\(1\)\)\);", "(M32(0x4a3820) = game_menu_step(M32(0x4a3820), -1));", 1),
        (r"\(M32\(0x4a3820\) = \(M32\(0x4a3820\) \+ 1\)\);", "(M32(0x4a3820) = game_menu_step(M32(0x4a3820), 1));", 1),
        (r"(\} while \(\(iVar4 < 0x80\)\);\n)(            VRETV\(puVar2\);\n        case 0x4:)", r"\1            game_menu_decorate();\n\2", 1),
        # port feature: menu back (sys.c game_menu_back): mode select -> title
        (r"(if \(\(\(MU32\(0x4a3aac\) & 0x30\) == 0\)\) \{\n)(\s+)(goto LAB_004311d6;)",
         r"\1\2if (game_menu_back(1)) (MU32(0x4a84f4) = 4);\n\2\3", 1),
        (r"if \(\(M32\(0x4a3820\) == 3\)\) \{", "if ((!game_menu_backing() && (M32(0x4a3820) == 3))) {", 1),
        (r"\(MU32\(0x4a3848\) = 3\);", "(MU32(0x4a3848) = game_menu_next(3));", 1)]),
    # stage select -> mode select, character select -> stage select
    ("g_430000.c", "431310", [
        (r"(        case 3:\n            ;\n            f_439240\(0x480340\);\n)",
         r"\1            if (game_menu_back(2)) { (M32(0x4a84fc) = 4); VRETV(iVar2); }\n", 1),
        (r"\(MU32\(0x4a3848\) = 4\);", "(MU32(0x4a3848) = game_menu_next(4));", 1)]),
    ("g_430000.c", "431910", [
        (r"(    switch \(M32\(0x4a8500\)\) \{\n)", r"\1        case 6:\n            game_menu_char_back();\n            VRET;\n", 1),
        (r"(            if \(\(M32\(0x4a84ec\) == 0\)\) \{\n                f_439240\(0x480340\);\n)",
         r"\1                if (game_menu_back(3)) {\n                    (MU32(0x488c68) = 1); (MF(0x488c6c) = 0.0f); (MU32(0x488c70) = 0x3ccccccd);\n                    (M32(0x4a8500) = 6);\n                    VRET;\n                }\n", 1)]),
    # port: the minimap (sprites 0x2c..0x5c, a 7x7 grid laid out by FUN_00439800 and coloured by
    # FUN_0043f4c0) is drawn as one centred block; per-sprite HUD anchoring would tear the grid
    # apart across the screen thirds and scale every tile up (dx.c im_hud_center)
    ("g_440000.c", "4406e0", [
        (r"(\s+)(COM_D3DDev2_DrawPrimitive\(MU32\(0x4a387c\), 5, 3, \(0x49b778 \+ \(uint32_t\)\(\(iVar2 \* 0x8c\)\)\), 4, 1\);)",
         r"\1im_hud_center(iVar2 >= 0x2c && iVar2 <= 0x5c);\1\2\1im_hud_center(0);", 1)]),
    # ring pages (magic 0x42c9f0, items 0x42cb50, 0x42ccb0): each icon's angle is fild [esp+0x18] =
    # ebx, the 0, 360, 720... counter; Ghidra used the slot index (or a string pointer), so every icon
    # sat within a degree of the first one while the slots stayed separately selectable
    ("g_42c000.c", "42c9f0", [
        (r"\(\(double\)\(M32\(_fr - 0xc\)\) \* \(double\)\(MF\(0x44d2bc\)\)\)", "((double)(iVar3) * (double)(MF(0x44d2bc)))", 1)]),
    ("g_42c000.c", "42cb50", [
        (r"\(\(double\)\(M32\(_fr - 0xc\)\) \* \(double\)\(MF\(0x44d2bc\)\)\)", "((double)(iVar3) * (double)(MF(0x44d2bc)))", 1)]),
    ("g_42c000.c", "42ccb0", [
        (r"\(\(double\)\(\(int32_t\)pcVar7\) \* \(double\)\(MF\(0x44d2bc\)\)\)", "((double)(iVar2) * (double)(MF(0x44d2bc)))", 1)]),
    ("g_42c000.c", "42cde0", [
        (r"    int32_t unaff_EBX = 0;\n", "", 1),
        (r"\n\s+\(unaff_EBX = \(unaff_EBX \* 0x168\)\);", "", 1),
        (r"unaff_EBX", "param_1", 3)]),
    # ring menu text: Ghidra mapped the actor argument ([esp+0x354]) onto local_4 after the
    # lstrcatA loops; local_4 itself is only ever written (0x100)
    ("g_430000.c", "432de0", [
        (r"M32\(_fr - 0x4\)(?! = 0x100)", "param_1", 17),
        # the pad string " " at [esp+0x12] (0x432e13, from 0x46bc74) was dropped
        (r"(\(MU8\(_fr - 0x31c\) = MU8\(0x478d64\)\);\n)", r"\1    (MU16(_fr - 0x33e) = MU16(0x46bc74));\n", 1),
        # the HDC slot as Ghidra spelled it (before the shift below changes the slot names)
        (r"CONCAT22\(MU16\(_fr - 0x32a\), CONCAT11\(MU8\(_fr - 0x32b\), MU8\(\(_fr - 0x32f\) \+ 3\)\)\)", "HDC_TEXT", 9),
        # from the page switch on, Ghidra's stack frame is 8 bytes low: the string buffers were
        # self-consistent, but the text rects ([esp+0x320] P1 / [esp+0x330] P2, set in the prologue)
        # were read 8 bytes early as (x, y, 0, 0), so DrawText got an empty rect and the ring text
        # (item names, LV/HP/EXP...) never showed; the accumulated string also missed its NUL init
        (r"(?s)\n    \(iVar3 = M32\(\(param_1 \+ 0x13c\)\)\);\n    \(iVar4 = \(iVar3 \* 0x68\)\);.*",
         lambda m: re.sub(r"_fr - 0x([0-9a-f]+)", lambda k: "_fr - 0x%x" % (int(k.group(1), 16) - 8), m.group(0)), 1),
        # DrawText of the 256x256 text texture path (0x4352ab): string [esp+0x34], rect [esp+0x340]
        (r"w_DrawTextA\(([^,]+), \(_fr - 0x334\), -\(1\), \(\(_fr - 0x30\) \+ \(uint32_t\)\(8\)\), 0x10\)",
         r"w_DrawTextA(\1, (_fr - 0x31c), -(1), (_fr - 0x10), 0x10)", 1)]),
    # sword trail (0x40aad7..0x40ac32): sample the blade by moving the actor's tip frame relative to
    # itself (-w, 0, -0.55 len) / (+w, 0, +0.55 len) / (0, 0, +0.75 len) / (0, 0, -0.75 len) and reading
    # its scene position twice. The emulated pushes overlapped the GetPosition output, so the tip
    # frame was left at ~1e29 and no sword hit could ever connect.
    ("g_408000.c", "40a730", [
        (r"(?s)\(MU32\(_fr - 0x98\) = MU32\(\(0x480374 \+ \(uint32_t\)\(iVar11\) \* 4\)\)\);\n.*?"
         r"\(MU32\(\(M32\(\(MU32\(\(0x4a3ca0 \+ \(uint32_t\)\(\(param_1 \* 0x2e\)\) \* 4\)\) \+ 8\)\) \+ 0x28\)\) = MU32\(\(\(_fr - 0xcc\) \+ \(uint32_t\)\(2\) \* 4\)\)\);\n",
         "(puVar14 = (0x480374 + (uint32_t)(iVar11) * 4));\n"
         "        {\n"
         "            uint32_t tip = MU32(puVar14), buf = MU32(MU32(0x4a3ca0 + param_1 * 0xb8) + 8);\n"
         "            float len = BITF(MU32(0x48bca8 + iVar9 * 0x80)), w = BITF(MU32(0x48bcac + iVar9 * 0x80));\n"
         "            COM_Frame2_SetPosition(tip, tip, BITU(-w), 0, BITU(len * MF(0x44d254)));\n"
         "            COM_Frame2_GetPosition(tip, MU32(0x4a388c), (_fr - 0x78));\n"
         "            COM_Frame2_SetPosition(tip, tip, BITU(w), 0, BITU(len * MF(0x44d250)));\n"
         "            for (int k = 0; k < 3; k++) MU32(buf + k * 4) = MU32(_fr - 0x78 + k * 4);\n"
         "            COM_Frame2_SetPosition(tip, tip, 0, 0, BITU(len * MF(0x44d24c)));\n"
         "            COM_Frame2_GetPosition(tip, MU32(0x4a388c), (_fr - 0x78));\n"
         "            COM_Frame2_SetPosition(tip, tip, 0, 0, BITU(len * MF(0x44d248)));\n"
         "            for (int k = 0; k < 3; k++) MU32(buf + 0x60 + k * 4) = MU32(buf + 0x20 + k * 4) = MU32(_fr - 0x78 + k * 4);\n"
         "        }\n", 1),
        # the same sampling again at 0x40ae23..0x40af4x (iVar11 = weapon id here)
        (r"(?s)    \(MU32\(_fr - 0x94\) = BITU\(\(float\)\(\(BITF\(MU32\(\(0x48bca8 \+ \(uint32_t\)\(\(iVar11 \* 0x20\)\) \* 4\)\)\) \* MF\(0x44d254\)\)\)\)\);\n.*?"
         r"    \(MU32\(\(M32\(\(MU32\(\(0x4a3ca0 \+ \(uint32_t\)\(\(param_1 \* 0x2e\)\) \* 4\)\) \+ 8\)\) \+ 0x28\)\) = uVar20\);\n",
         "    {\n"
         "        uint32_t tip = MU32(puVar14), buf = MU32(MU32(0x4a3ca0 + param_1 * 0xb8) + 8);\n"
         "        float len = BITF(MU32(0x48bca8 + iVar11 * 0x80)), w = BITF(MU32(0x48bcac + iVar11 * 0x80));\n"
         "        COM_Frame2_SetPosition(tip, tip, BITU(-w), 0, BITU(len * MF(0x44d254)));\n"
         "        COM_Frame2_GetPosition(tip, MU32(0x4a388c), (_fr - 0x84));\n"
         "        COM_Frame2_SetPosition(tip, tip, BITU(w), 0, BITU(len * MF(0x44d250)));\n"
         "        for (int k = 0; k < 3; k++) MU32(buf + k * 4) = MU32(_fr - 0x84 + k * 4);\n"
         "        COM_Frame2_SetPosition(tip, tip, 0, 0, BITU(len * MF(0x44d24c)));\n"
         "        COM_Frame2_GetPosition(tip, MU32(0x4a388c), (_fr - 0x84));\n"
         "        COM_Frame2_SetPosition(tip, tip, 0, 0, BITU(len * MF(0x44d248)));\n"
         "        for (int k = 0; k < 3; k++) MU32(buf + 0x60 + k * 4) = MU32(buf + 0x20 + k * 4) = MU32(_fr - 0x84 + k * 4);\n"
         "    }\n"
         "    (fVar3 = MF(0x44d1e4));\n"
         "    (iVar8 = 0);\n", 1)]),
    # attack handlers fn(actor) from the state tables: Ghidra invented a thiscall `this` that is really
    # the weapon id the original saves into the argument's stack slot at entry (right / left hand)
] + [(fn, a, [
        (r"\(uint32_t this, uint32_t param_1\)\n\{\n    VNOFRAME;\n",
         "(uint32_t param_1)\n{\n    VNOFRAME;\n    uint32_t this = MU32(0x49fee4 + (M32(%s + M32(param_1 + 0x13c) * 0xd00) + M32(param_1 + 0x13c) * 0x340) * 4);\n" % tbl, 1)])
     for fn, a, tbl in (("g_420000.c", "4202e0", "0x49fecc"), ("g_41c000.c", "41fd10", "0x49fed0"), ("g_418000.c", "418330", "0x49fed0"))] + [
    # effect pieces fade (states 2/3): z offset is fild [esp+0x68], the argument slot that holds the
    # loop counter (ebp, iVar10); Ghidra read the never-written local _fr-0x4, so all pieces sat at z=0
    ("g_40c000.c", "40c3a0", [
        (r"\(float\)\(M32\(_fr - 0x4\)\)", "(float)(iVar10)", 2)]),
    # projectile (effect kind 8): each trail particle sits at z = len * speed * phase * count behind
    # the head (0x409d01 fmul [part+0x34], a float); Ghidra converted the phase's bits as an int, so
    # the visible particles were placed ~1e9 units away and the shot never showed travelling
    ("g_408000.c", "4097e0", [
        (r"\(float\)\(MU?32\(\(\(MU32\(\(0x4a3ca0 \+ \(uint32_t\)\(\(param_1 \* 0x2e\)\) \* 4\)\) \+ iVar6\) \+ \(uint32_t\)\(0xd\) \* 4\)\)\)",
         "MF(((MU32((0x4a3ca0 + (uint32_t)((param_1 * 0x2e)) * 4)) + iVar6) + (uint32_t)(0xd) * 4))", 2)]),
    # HUD HP bars of the last-hit enemies (0x43ff58.., 0x440003..): fild of hp / max hp. Ghidra used
    # the raw int bits as floats; on PC the denormal ratio survived, under PSP flush-to-zero it was
    # 0/0 = NaN and the enemy bar never appeared
    ("g_43c000.c", "43fd60", [
        (r"BITF\(M32\(\((0x480410|0x48040c)", r"(float)(M32((\1", 8)]),
    # number display: the ones digit is an integer (mov [esp+0x5c],edx; fild) but Ghidra stored its
    # bits into a float slot, so the digit converted back as a denormal = 0
    ("g_408000.c", "40b6c0", [
        (r"\(MF\(_fr - 0x18\) = BITF\(\(\(int64_t\)", "(MF(_fr - 0x18) = (float)(((int64_t)", 1)]),
    # attack handler: ebp = weapon id saved at entry ([esp+0x10] = ebx from the first statement)
    ("g_420000.c", "420950", [
        (r"(\(iVar10 = MU32\(\(0x49fee4 [^\n]*\n)", r"\1    (unaff_EBP = iVar10);\n", 1)]),
]
PROTOS = [(r"^((?:void|uint32_t) f_42(?:48f0|4b30|4c60|4e00|4fe0|50f0|51f0))\(uint32_t param_1\);",
           r"\1(uint32_t param_1, int32_t slot);"),
          (r"^(uint32_t f_4252f0)\(uint32_t param_1\);", r"\1(uint32_t param_1, uint32_t px, uint32_t py, uint32_t pz);"),
          (r"^(uint32_t f_(?:4202e0|41fd10|418330))\(uint32_t this, uint32_t param_1\);", r"\1(uint32_t param_1);")]
PATCHES_LATE = [
    # 0x40c800: both branches put the camera position (in effect space) at [esp+0x2c] and face the
    # pieces along (-v.x, 0, -v.z); Ghidra's offsets for that vector disagree, so use one slot
    ("g_40c000.c", r"(COM_Frame2_GetPosition\(MU32\(0x4a3898\), MU32\(\(0x4a3c98 \+ \(uint32_t\)\(\(param_1 \* 0x2e\)\) \* 4\)\), )\(_fr - 0x5c\)\);",
     r"\1(_fr - 0x90));", 1),
    ("g_40c000.c", r"BITU\(-\(MF\(_fr - 0x70\)\)\), 0, BITU\(-\(MF\(_fr - 0x68\)\)\)", "BITU(-(MF(_fr - 0x90))), 0, BITU(-(MF(_fr - 0x88)))", 2),
    # ring menu pages: hand port f_42c7e0(player, page) (the decompile added a fake `this`)
    ("g_428000.c", r"f_42c7e0\(this, param_1, ([01])\);", r"f_42c7e0(param_1, \1);", 2),
    ("g_438000.c", r"f_4252f0\(0x480340\);", "f_4252f0(0x480340, 0x3f800000, 0x41200000, 0);", 1),
    ("g_438000.c", r"f_4252f0\(0x480484\);", "f_4252f0(0x480484, 0xbf800000, 0x41200000, 0);", 1),
    # perf log: time the per-tick collision pass (port instrumentation, src/game/sys.c)
    ("g_428000.c", r"\n            f_429a00\(\);\n", "\n            prof_collide();\n", 1),
    # port feature: the ring menu (scaled open/closed by these three) is drawn larger on the PSP
    ("g_42c000.c", r"COM_Frame2_AddScale\(MU32\(\(0x48bb14 \+ \(uint32_t\)\(\(param_1 \* 0x68\)\)\)\), 0, BITU\(fVar1\), BITU\(fVar1\), BITU\(fVar1\)\);",
     "COM_Frame2_AddScale(MU32((0x48bb14 + (uint32_t)((param_1 * 0x68)))), 0, BITU(fVar1 * PLAT_HUD_SCALE), BITU(fVar1 * PLAT_HUD_SCALE), BITU(fVar1 * PLAT_HUD_SCALE));", 3),
] + [
    # int -> float is a signed 32-bit fild in every one of these (damage = weapon power + bonus at
    # 0x419cb0/0x419f70/0x41b590, effect piece count at 0x406b50): an unsigned conversion
    # turns a negative sum into ~4e9
    (fn, r"\(float\)\((\(?)MU32\(", r"(float)(\1M32(", n)
    for fn, n in (("g_404000.c", 1), ("g_418000.c", 4))
] + [
    # actor+0xe4 (attack power) is an integer: all 113 loads in the asm are fild. Ghidra read its
    # bits as a float, so attack powers were denormals: enemy attacks did ~no damage on PC, and on
    # the PSP (flush-to-zero) projectile power was 0, failing their hit gate (0x4099d8), so enemy
    # fireballs never hit
    (fn, r"BITF\(MU?32\(\(param_1 \+ \(uint32_t\)\(0x39\) \* 4\)\)\)", "(float)(M32((param_1 + (uint32_t)(0x39) * 4)))", n)
    for fn, n in (("g_40c000.c", 13), ("g_410000.c", 4), ("g_414000.c", 11), ("g_41c000.c", 66), ("g_420000.c", 18))
] + [
    # the same for actor+0xdc (24 fild in the asm): the power f_415300 passes to FUN_00428ba0 (0x41553b)
    ("g_414000.c", r"BITF\(MU?32\(\(param_1 \+ \(uint32_t\)\(0x37\) \* 4\)\)\)", "(float)(M32((param_1 + (uint32_t)(0x37) * 4)))", 1),
]


def apply_func_patches():
    ok = True
    for fn, addr, reps in FUNC:
        path = "src/game/gen/" + fn
        s = open(path).read()
        m = re.search(r"/\* ---- FUN_00%s ---- \*/\n.*?(?=/\* ---- FUN_|\Z)" % addr, s, re.S)
        if not m:
            print("func patch: FUN_%s not found in %s" % (addr, fn)); ok = False; continue
        body = m.group(0)
        for pat, rep, n in reps:
            body, k = re.subn(pat, rep, body)
            if n >= 0 and k != n:
                print("func patch mismatch in %s: %r matched %d (expected %d)" % (addr, pat, k, n)); ok = False
        open(path, "w").write(s[:m.start()] + body + s[m.end():])
    p = "src/game/gen/protos.h"
    s = open(p).read()
    for pat, rep in PROTOS:
        s = re.sub(pat, rep, s, flags=re.M)
    open(p, "w").write(s)
    return ok


def main():
    ok = apply_func_patches()
    for fn, lit, reps in NTH:
        path = "src/game/gen/" + fn
        s = open(path).read()
        for r in reps:
            if lit not in s:
                print("nth patch: %r not found in %s" % (lit, fn)); ok = False; break
            s = s.replace(lit, r, 1)
        open(path, "w").write(s)
    for fn, pat, rep, n in PATCHES + PATCHES_LATE:
        path = "src/game/gen/" + fn
        s = open(path).read()
        s2, k = re.subn(pat, rep, s)
        if n >= 0 and k != n:
            print("patch mismatch in %s: %r matched %d (expected %d)" % (fn, pat, k, n))
            ok = False
        open(path, "w").write(s2)
    # comparisons with denormal float literals are integers misread as floats: they fail on the PSP
    # (plain stores of such constants, e.g. emulated return-address pushes, are harmless)
    import glob
    for path in sorted(glob.glob("src/game/gen/g_*.c")):
        for n, line in enumerate(open(path), 1):
            if re.search(r"(?:[=!<>]=|[<>])\s*-?\d(?:\.\d+)?e-(?:3[89]|4\d)\b|\d(?:\.\d+)?e-(?:3[89]|4\d)\s*(?:[=!<>]=|[<>])", line):
                print("denormal literal (integer typed as float?) at %s:%d" % (path, n)); ok = False
    sys.exit(0 if ok else 1)

main()
