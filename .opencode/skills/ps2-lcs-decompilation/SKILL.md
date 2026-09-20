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
