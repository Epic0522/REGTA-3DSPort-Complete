---
name: ps2-lcs-decompilation
description: Extract and reverse-engineer the original PS2 GTA Liberty City Stories disc (SLUS_214.23 boot ELF, main.scm, ISO9660 filesystem) to establish authoritative ground truth for behavior questions in the reLCS 3DS port. Use when the user asks "what does the original/PS2 game actually do", questions a value/colour/constant/behavior against the real game, provides or references a PS2 LCS ISO, asks to decompile/disassemble/RE the PS2 binary, or when reLCS source appears to have inherited a wrong value from its Vice City ancestor and no fork/decompilation project has already fixed it.
---

# PS2 LCS decompilation

reLCS (`stories/`) is a from-scratch reimplementation, not a decompilation —
it was bootstrapped from reVC (`miami/`) and inherited some VC-specific
constants/behavior that were never corrected for LCS (see the 3D-marker
arrow-colour case study below, a textbook example: the pink arrow color was
literally copy-pasted from Vice City into every public reLCS fork on GitHub,
including reference forks like `knackers4/res`, because nobody had ever
checked it against the real PS2 game).

**When public source forks don't have the answer, the PS2 disc does.** This
skill covers extracting and disassembling it.

## When source archaeology isn't enough

Before reaching for the ISO, exhaust the cheap options: `git log` on the
suspect files, `grep` for the constant across public re3/reVC/reLCS forks via
`gh search code`, and check whether the "obviously wrong" value is actually
shared verbatim by VC (a strong signal it's inherited, not LCS-original — see
`stories/src/core/Radar.h`'s `MARKER_COLOR_*` history for a real example).
Only reach for the disc once you've confirmed no existing project has already
resolved the question.

## Tools

All scripts live in `scripts/` next to this file and have no dependency on
this repo's build system — they're standalone Python, run from any directory
once you pass full paths (or `cd` into `scripts/` so the plain
`import elf_tools` works). Only third-party dependency: `pip install
capstone` for disassembly (extraction and data-table reads work without it).

### `scripts/iso9660.py` — extract files from the disc

The disc is plain ISO9660 (no RockRidge/Joliet needed for PS2 discs).

```sh
python3 scripts/iso9660.py list   "/path/to/GTA LCS (USA).iso"
python3 scripts/iso9660.py extract "/path/to/GTA LCS (USA).iso" /SLUS_214.23 /tmp/SLUS_214.23
python3 scripts/iso9660.py extract "/path/to/GTA LCS (USA).iso" /DATA/MAIN.SCM /tmp/main.scm
```

Known layout of the USA LCS disc: boot ELF at `/SLUS_214.23` (~2.99 MB),
script at `/DATA/MAIN.SCM`, text at `/TEXT/ENGLISH.GXT`, models in
`/MODELS/*.IMG`. Other regions/revisions will have a different SLUS/SLES
number but the same rough layout — `list` first to confirm.

### `scripts/elf_tools.py` — load and disassemble the boot ELF

The boot ELF is a stripped, statically-linked MIPS III (PS2 "Emotion Engine")
executable. Section headers and the linker `$gp` value (needed for
`lw $t0,-0x828($gp)`-style loads — the EE's small-data-area addressing) are
parsed directly from the ELF, not hardcoded, so this works across disc
revisions without editing the script.

```python
import sys; sys.path.insert(0, 'scripts')
import elf_tools as E
E.load('/tmp/SLUS_214.23')
E.gp                          # linker $gp, e.g. 0x3d6ef0
E.dis(0x118380, 60)           # disassemble 60 insns at a VA (needs capstone)
E.words(0x3cf7c8, 4)          # read raw data words (jump tables, const tables)
E.find_addr_refs(0x3cf7c8)    # VAs of `lui;<imm-op>` pairs that build this address
```

CLI form: `python3 elf_tools.py <elf> dis|words|refs <hexva> [count]`.

**`dis_robust()`/CLI `dis`** must be used over plain `dis()` for anything
beyond a quick peek: the PS2 EE has 128-bit MMI/COP2 instructions capstone's
plain MIPS32 mode can't decode, and it will simply stop at the first one.
`dis_robust` prints `.word 0x...` for those and keeps going.

**`find_addr_refs(target)`** is how you find *who references* a data address
in a binary with no relocation table left — e.g. "what code reads this RGBA
colour constant". It only catches the `lui $rt,HI16` + (`addiu`|`ori`|load/store)
`$rt,LO16($rt)` pattern; gp-relative loads (`lw $t0,IMM($gp)`) need a
different scan (grep disassembly output for `($gp)` near the byte offset you
computed from `target - gp`) since there's no `lui` to anchor on.

### `scripts/scm_disasm.py` — disassemble `main.scm`

Parses the opcode table straight from `stories/src/control/ScriptDebug.cpp`'s
`REGISTER_COMMAND` macros (so it always matches whatever opcode set this port
implements) and decodes self-describing parameters per the `ARGUMENT_*`
scheme in `stories/src/control/Script.h`.

```python
import scm_disasm as S
names, args = S.load_opcode_table('/path/to/stories/src/control/ScriptDebug.cpp')
d = S.load_scm('/path/to/main.scm')
start = S.first_instruction_offset(d)   # script always opens with a GOTO past the mission table
for opname, params, nexti in S.disasm_linear(d, names, args, start):
    print(opname, params)
```

**Only trust linear disassembly from a known-good anchor** (offset 0's GOTO
target). SCM opcodes have no alignment/sync markers, so scanning forward from
an arbitrary offset and trying to "validate" a candidate stream is unreliable
— it produced both false negatives and false positives when tried (see the
case study). If you need to read at an arbitrary offset, decode linearly from
the nearest confirmed instruction boundary, not from the target offset
directly.

**Known limitation:** `load_opcode_table()`'s argument lists come from
`ScriptDebug.cpp`'s `REGISTER_COMMAND` table, which is inherited from reVC and
is *wrong* for a nontrivial number of LCS-only opcodes (wrong arg count or
wrong types) — `disasm_linear` will desync a few instructions after any such
opcode. If you hit a desync, don't assume the anchor was wrong; check whether
the offending opcode's actual argument count (derived from its C++ `case`
body: `CollectParameters`/`StoreParameters` counts, `GetPointerToScriptVariable`
calls, `ReadTextLabelFromScript` = 8 bytes each) matches what
`ScriptDebug.cpp` claims before concluding the anchor or decoder is broken.

#### `main.scm` binary layout (verified against the real file)

- Raw file starts with an 8-byte header: `uint32 MainScriptSize`, `uint32
  LargestMissionSize`. `load_scm()` strips these 8 bytes, so offset 0 in its
  returned bytes is ScriptSpace offset 0 (what GOTO/JSR targets are relative
  to) — **not** the raw file offset.
- **Mission table**: at raw file offset `MainScriptSize + 8 + 20`, preceded
  by `uint32 LargestMissionSize` (repeated) and `uint32 missionCount`. Table
  is `missionCount` `uint32` entries, each one a **ScriptSpace offset** for
  that mission's code.
- **Raw-file offset of mission `i`'s code**: `table[i] + 8 + local_offset`
  (the `+8` re-adds the header that `load_scm()`/ScriptSpace strips — do not
  forget it when seeking in the *raw* file, as opposed to indexing into
  `load_scm()`'s returned bytes, which need no adjustment).
- **Runtime `m_nIp` for mission code**: `MainScriptSize + local_offset`
  (mission bytecode is loaded starting right after the main script in
  `ScriptSpace`).
- Opcodes are 2 bytes little-endian; bit `0x8000` is the NOT flag (strip it
  before indexing `names`/`args`). `CRunningScript::ProcessOneCommand`
  dispatches to `ProcessCommandsXToY` in bins of the form `<100, <200, <305,
  <405, ...` — note the bin width creeps by 5 starting at 300, it is not a
  uniform `<(N+100)`.
- Parameter type-byte payload widths (`ARGUMENT_*` in `Script.h`, verified
  against `CollectParameters`): END/INT_ZERO/FLOAT_ZERO = 0 bytes;
  FLOAT_1BYTE=1; FLOAT_2BYTES=2; FLOAT_3BYTES=3; INT32/FLOAT=4; INT8=1;
  INT16=2; TIMER=0; LOCAL=0; **LOCAL_ARRAY=2** extra bytes (index_id, size);
  **GLOBAL=1** extra byte (global index low byte); GLOBAL_ARRAY=3 extra
  bytes (index_in_block, index_id, size). `ARGTYPE_STRING`/`ARGTYPE_TEXT_LABEL`
  parameters (not encoded via the type-byte scheme) are a fixed 8 raw bytes.
- `first_instruction_offset()`'s target is itself a self-describing param
  (type byte + payload), not a bare int32 immediately after the opcode —
  decode it with `_read_param`, the same as any other instruction operand.

These facts (and the `first_instruction_offset`/`LOCAL_ARRAY`/`GLOBAL` byte-
width bugs they exposed) came from reverse-engineering the TR1 ("Wong Side of
the Tracks") mission-23 spawn-loop crash; see `git log --grep=TR1` /
`--grep="Wong Side"` for the investigation if more script-VM archaeology is
needed later.

## Workflow for a "what does PS2 actually do" question

1. Extract the boot ELF (`iso9660.py extract ... /SLUS_XXX.XX ...`).
2. Get its section map + `gp` (`elf_tools.load`).
3. Find the reLCS C++ function you're questioning (e.g.
   `stories/src/core/Radar.cpp`'s `Draw3dMarkers`), and use its *structure*
   as a map: reLCS was reimplemented against the real game's behavior at
   some point in the past, so struct sizes, call order, and switch-case
   counts usually still line up even when a specific constant/branch was
   later corrupted or left un-ported. Confirm struct layout by cross-checking
   a field reLCS *does* get right (e.g. verify `sRadarTrace`'s stride/offsets
   in the PS2 binary by finding where a known-correct field like
   `m_eBlipType` is read, then use that same base to read the questioned
   field).
4. Disassemble the equivalent PS2 function (search for it via a stable
   anchor: an already-known-correct constant it references, or a jump table
   in `.rodata`/`.text` with the right number of case targets, found via
   `find_addr_refs` on something the function is known to touch).
5. Trace data references (`find_addr_refs`, `words`) to pull out the actual
   constant tables — PS2 GTA engines keep small colour/tuning tables in
   `.sdata`/`.rodata` as flat structs, often with padding (e.g. LCS's 3D
   marker RGBA table is 4-byte colour + 4-byte pad, 8-byte stride).
6. Cross-check: does the PS2 logic reduce to the reLCS logic under reLCS's
   *current* (possibly wrong) variable values? If yes, sometimes only the
   constant needs fixing. If the PS2 code branches on a variable reLCS
   force-overwrites or never plumbs through, the fix needs to touch that too
   — a constants-only patch would still be wrong. (This exact situation
   happened with the arrow colours: reLCS's `SetEntityBlip` force-set
   `m_nColor` to one fixed value, discarding the very argument PS2's version
   stores, which made recoloring the constants alone insufficient.)

## Case study: 3D-marker arrow colours (reference example)

Objective arrows rendered Vice-City pink in `stories/` because
`Radar.h`'s `CARBLIP/CHARBLIP/OBJECTBLIP_MARKER_COLOR_*` were the VC pink
`(252,138,242)`, unchanged by every public reLCS fork checked (including
`knackers4/res`). Reversing `SLUS_214.23` found:

- A 4-entry RGBA palette in `.sdata` (8-byte stride): yellow, red, blue,
  green — the game's actual "various colors" per blip type.
- `Draw3dMarkers`'s PS2 equivalent picks color from `m_nColor`: cars/chars
  are red-or-blue by `m_nColor != 0`, objects are red/blue/green by
  `m_nColor == 0 / == 2 / else`.
- Critically, PS2's `SetEntityBlip` **stores its `color` argument** into
  `m_nColor`; reLCS's version **force-overwrites it** to a constant. Fixing
  only the palette constants would have made every arrow the same wrong
  color — the fix had to also stop reLCS from discarding the real argument,
  and fix the two script opcodes (`ADD_BLIP_FOR_CHAR`/`_OBJECT`) that were
  passing VC's color indices instead of LCS's.

Full technical derivation (palette VAs, function VAs, per-case logic,
mapped source line numbers) is in this repo's git history — see the commit
`stories: fix 3D-marker arrow colours to match PS2 LCS`.

## Pitfall: generic one-argument setters are easy to swap

Small setter functions (`SetFoo(int16)`, one argument, one store, `jr $ra`)
are extremely common in PS2 GTA's `CFont`/`CHud`/`CFontDetails`-style classes,
and at a `jal` call site they are *indistinguishable from each other* — same
calling convention, same tiny size, same "load an immediate into `$a0`, `jal`"
shape. Do not identify one by call-site pattern alone (e.g. "the block that
sets colours also calls a function with argument 2, so that must be
`SetFontStyle`"). Two real setters can look identical from the caller's side
while writing to completely different struct fields.

**Case study: `CFont::SetFontStyle` vs `CFont::SetDropShadowPosition` (LCS
big-message HUD slots).** While reversing PS2's per-slot text styling for
`CHud::Draw`/`DrawAfterFade`, two VAs were initially assigned by guesswork
from call order and got swapped: `0x166e50` was assumed to be
`SetFontStyle` and `0x166ca8` was assumed to be `SetDropShadowPosition`.
Both are one-argument setters called via `jal` with a small integer
immediate (0, 1, or 2) in the delay slot, so nothing about the call site
distinguished them. The swap silently transposed the decoded font-style and
drop-shadow values for every HUD slot, and a fix built on that wrong mapping
(commit `1773063`) shipped a real regression (mission title rendered in the
wrong typeface, `all-caps` where PS2 renders mixed case) before the mistake
was caught from a side-by-side screenshot comparison.

The reliable disambiguation method, once the ambiguity is suspected:
1. **Disassemble the callee itself**, not just the call site. `0x166ca8`'s
   body does `sra $a0,$a0,0x10; bne $a0,$v0,...` (a comparison against a
   constant, branching to one of two stores) — structurally identical to
   reLCS's own `CFont::SetFontStyle` (`Font.cpp:1518-1529`, which branches on
   `style == FONT_HEADING` to also set `bFontHalfTexture`). `0x166e50` is a
   single unconditional `sh $a0, OFFSET($v0)` with no branch — a plain field
   write, matching `SetDropShadowPosition`'s one-line body
   (`Font.cpp:1582-1586`).
2. **Match the store offset against the known struct layout.** Once
   `CFontDetails`'s base VA is found (from any confirmed field access), each
   setter's write offset can be checked against the reLCS field order in
   `Font.h` (`style` and `bFontHalfTexture` sit together; `dropShadowPosition`
   is a separate field further down the struct). A setter that writes two
   fields conditionally is not a single-field setter like
   `SetDropShadowPosition`.
3. **Cross-check against a call site with a visually verifiable result.** A
   font-style guess can be confirmed or falsified by comparing an in-game
   screenshot against the PS2 original for one text string whose typeface is
   unambiguous (e.g. a zone name known to render in one specific reLCS font).
   Do this *before* trusting a style-index decode enough to change several
   call sites based on it — one confirmed mapping generalizes to every other
   call of the same VA in the binary.

General rule: when a struct has multiple `int16`/`bool`/enum-typed fields
set by separate one-line setters, resolve their VAs by *behavior* (branch
structure, store offset) before generalizing a guessed identity across many
call sites — a single swapped identity corrupts every decode that depends on
it.

## Case study: PS2 disassembly proving a port mechanism doesn't exist at all

Not every investigation resolves into a wrong constant or swapped VA —
sometimes the disassembly proves reLCS invented a whole mechanism PS2 never
had, and that mechanism is the bug itself.

**TR1 ("Wong Side of the Tracks") intermittent mis-spawn**: the player would
occasionally start the race still on the mission Sanchez but at the wrong
location, instantly failing. `scm_disasm.py` found the trigger script
(`TCHRM`) latches global `$3761` from `$2289` (the player's live vehicle
handle, refreshed every frame by a second thread, `CARM`) right before
launching the mission — a same-frame dependency between two independently-
scheduled script threads. That pointed at `stories/src/control/Script5.cpp`:
`SaveAllScripts` writes `pActiveScripts` head→tail, but `LoadAllScripts`
restores through `StartNewScript`, which head-inserts — every save/load
round-trip reverses the list, and which of the two threads runs first
depends on parity.

Disassembling PS2's equivalent block loader (`SLUS_214.23` VA `0x300c10`,
called from `GenericLoad`) proved **PS2's restore step never touches the
running-script list at all** — it restores only registered save-vars (via a
`GetSaveVarIndex` filter table) and zeroes every other global; the per-script
records PS2's `SaveAllScripts` equivalent writes (`0x300ba8`) are written but
never read back on load. PS2 rebuilds its script-thread list from scratch on
every load by re-running main.scm's bootstrap, which always yields the same
(creation) order. **There was no PS2 restore-order to match — the entire
list-reversing mechanism in reLCS is a port-only invention**, non-
deterministically putting the two racing threads in either order depending
on how many save/load round-trips had happened.

Fix: restore `pActiveScripts` into descending `CRunningScript::m_nId`
(a strictly-increasing creation-order counter) after the restore loop, the
invariant the live list always holds outside of a load — matching PS2's
always-creation-order rebuild. See `SortScriptListByCreationOrder` in
`stories/src/control/Script5.cpp` and
`scripts/tests/test_script_load_order.py`.

**Lesson:** when a cross-thread/cross-system interaction doesn't resolve
from source alone, disassemble the *equivalent PS2 code path* — it can prove
the mechanism doesn't exist on PS2 in the form you assumed, which is more
decisive than finding the "right" constant, because the whole mechanism is
suspect, not just a value.
