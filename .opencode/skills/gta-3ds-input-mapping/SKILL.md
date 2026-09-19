---
name: gta-3ds-input-mapping
description: Explains how 3DS button input maps to in-game menu accept/cancel, shop prompts, and script pad polling across the III (re3), miami (reVC), and stories (reLCS) game trees. Use when changing what button confirms/cancels a menu, remapping Circle/Cross/Square/Triangle, editing Pad.h/Pad.cpp/Frontend.cpp/ControllerConfig.cpp/Script5.cpp, fixing a shop or Ammu-Nation button prompt, investigating why a printed button letter (A/B/X/Y) doesn't match what works, or asking about CPad::GuiSelect/GuiBack/AffectFrom3DS/Nintendo3DSButtons.
---

# GTA 3DS input mapping (menu accept/cancel and shops)

There is **no C++ shop UI code** in any of the three trees (no `CShopping`,
`CClothes`, tattoo/barber classes). Ammu-Nation, clothes shops, tattoo,
barber, food, the ferry, and time-trial kiosks are all implemented in
`main.scm` and reach C++ through exactly two opcode seams — see Seam 3 below.
Garages, phones, radio, and pickups use named control *actions* (fire
weapon, collect pickup, change station), not accept/cancel, and are
unaffected by anything in this skill.

## Physical button map — DIVERGES between trees

`core/Pad.cpp`, function `CPad::AffectFrom3DS()`:

- **stories (reLCS)**: `Cross=KEY_B, Circle=KEY_A, Square=KEY_Y, Triangle=KEY_X`
- **III (re3) / miami (reVC)**: `Cross=KEY_A, Square=KEY_B, Triangle=KEY_Y, Circle=KEY_X`

The three trees are NOT in sync on this. Any change to one tree's physical
map does not apply to the others, and III/miami already use Nintendo-style
accept=A at the physical layer while stories historically did not.

**Do not change the physical map to move a menu button.** It invalidates
every `~k~` gameplay hint text (via `Nintendo3DSButtons`, below) and the
sniper-zoom label overrides — not just the menu you're touching. Prefer the
semantic seams below.

## The four semantic seams (stories/src)

1. **`core/Pad.h`** — 3DS-port-added `Gui*` helpers (absent in miami/III):
   `GuiSelect()`, `GuiBack()`, `GetSkipCutscene()`, plus `GuiLeft/Right/Up/Down`.
   Exposed to the LCS script VM at `control/Script8.cpp` as
   `COMMAND_GET_PAD_BUTTON_STATE` cases 42 (GuiSelect), 43 (GuiBack), 44
   (GetSkipCutscene).

2. **`core/Frontend.cpp`** — a macro block (~line 168) defines
   `GetBackJustUp`/`GetBackJustDown` (and, after the menu remap, also
   `GetAcceptJustUp`/`GetAcceptJustDown`) across 4 platform branches (`_3DS`,
   `TRIANGLE_BACK_BUTTON`, `CIRCLE_BACK_BUTTON`, `#else`). `#undef`'d at end
   of file. This is what the PC-style frontend menus (pause menu, save menu,
   options) actually read.

3. **`control/Script5.cpp`**, `CRunningScript::GetPadState(pad, button)` —
   legacy shop scripts poll this via `COMMAND_IS_BUTTON_PRESSED`
   (`COMMAND_GET_PAD_STATE` is commented out, unused). Slots: 14=Square,
   15=Triangle, 16=Cross, 17=Circle, 18=LeftShock, 19=RightShock. This is the
   **only path shops (Ammu-Nation, clothes, ferry, time trials) use** to read
   accept/cancel. miami/III have an equivalent function with the same slot
   numbering.

4. **Button-label text** — two places render what letter a prompt shows:
   - `text/Messages.cpp`, `fixedTokens[]` array: hardcoded ASCII labels for
     `~k~` tokens (Mode-independent), e.g. `{"AMBUY","A"}` (Ammu-Nation
     buy), `{"AMEXI","B"}` (Ammu-Nation exit), `{"TRSK","A"}` (ferry skip).
     These drive GXT strings like `A_H1`, `CLOTHA`, `VIC4_A4`, `TT1_A0`,
     `FERR_SK`.
   - `core/ControllerConfig.cpp`, `GetWideStringOfCommandKeys()`, the
     `#ifdef _3DS` early-return block indexing the `Nintendo3DSButtons[][]`
     table (Mode-dependent, covers most gameplay `~k~` tokens including the
     phone hang-up label via a `VEHICLE_ENTER_EXIT &&
     ArePlayerControlsDisabled()` special case, and sniper zoom via
     dedicated `PED_SNIPER_ZOOM_IN`/`_OUT` special cases).

If you move what button accepts/cancels a menu or shop, you must update BOTH
the input-reading seam (1/2/3) AND the matching label text (4) — the two are
never automatically consistent. A previous conflict found this way: `TRSK`'s
label tracked `GuiSelect`, and `AMBUY`/`AMEXI` tracked
`GuiSelect`/`GuiBack` — changing the Gui* seam without updating
`fixedTokens[]` leaves the printed letter wrong even though the button
itself works correctly.

## Known behavioral risk when remapping the accept button

`CPad::GetWeapon()` (`core/Pad.cpp`) reads **held** `NewState.Circle` (not
edge-triggered) in Mode 0/1, gated only by `ArePlayerControlsDisabled()`.
`Frontend.cpp` calls `CPad::GetPad(0)->Clear(false)` when a menu closes,
which zeroes Old/NewState — so a held accept button at menu-close produces a
fresh edge-trigger next frame. If the menu accept button is ever set to the
same physical button as fire/weapon-select, closing a menu with that button
held can fire a weapon. `Frontend.cpp`'s pause-menu option-0 resume path
already special-cases this by using `JustUp` instead of `JustDown`; other
resume paths do not. The `JustOutOfFrontend` field in `Pad.h`/`Pad.cpp`
exists for exactly this kind of guard but is never armed anywhere in the
codebase (dead machinery) — arming it is the correct fix if this risk needs
mitigating rather than just tested around.

## Where NOT to look

- `Frontend_PS2.cpp` is dead code (`PC_MENU` is defined, so `PS2_MENU` never
  compiles). Its `~T~`/`~X~` PlayStation-glyph button text is unreachable.
- `ModifyStringLabelForControlSetting` (`control/Pickups.cpp`) rewrites GXT
  key suffixes `_L/_T/_C` by `CPad::Mode`, but no such key family exists in
  the LCS GXT — dead path.
- `NameGrid.cpp`'s `ProcessDPadCrossJustDown` is an empty stub; `CGrid` is
  never instantiated.
- `CREATE_MENU`/`SET_ACTIVE_MENU_ITEM` opcodes do not exist in this codebase
  (San Andreas-only) — shop UIs draw their own text and poll pad state
  directly via seam 3.
