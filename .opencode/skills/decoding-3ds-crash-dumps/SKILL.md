---
name: decoding-3ds-crash-dumps
description: Use when a 3DS build of re3/reVC/reLCS (III/miami/stories) has crashed and produced a crash_dump_*.dmp file from Luma3DS, when you need to decode ARM register state and an exception type (DATA ABORT, PREFETCH ABORT, undefined instruction) from such a dump, or when you need to symbolize a crash PC/LR against a matching .elf/.map to find the faulting function, source line, and call stack.
---

# Decoding 3DS crash dumps

Luma3DS writes a `crash_dump_NNNNNNNN.dmp` file to the SD card root when a
homebrew/CIA process crashes. It is a small (~700 byte) binary file, not a
core dump — no memory image beyond a short code/stack window. This skill
covers decoding it and mapping the fault back to source.

## Prerequisite: get the exact matching .elf

The dump has no build ID. You MUST use the `.elf` from the exact build that
was running on the console, or PC/LR symbolization will silently produce
wrong answers. `stories/build/relcs.elf` (or the equivalent for III/miami)
is overwritten by every `scripts/build.sh` run — if in doubt, check `md5sum`
against any `.elf` copies the user may have kept (e.g. in `out/` or a
packaging output dir) before trusting a symbolization.

## Header (40 bytes, all little-endian uint32 unless noted)

| Offset | Field | Notes |
|---|---|---|
| 0 | magic | `0xdeadc0de` or `0xdeadcafe` |
| 4 | version | e.g. `0x00010003` |
| 8 | processor | `11` = ARM11 (userland) |
| 12 | type | exception type: `3` = DATA ABORT (others exist for prefetch abort / undefined instruction — same register layout, different `type`) |
| 16 | totalSize | total file size |
| 20 | regDumpSize | size of the register block that follows (92 bytes = 23 words) |
| 24 | codeDumpSize | size of the code-bytes block (bytes ending at `pc`) |
| 28 | stackDumpSize | size of the stack-bytes block (bytes starting at `sp`) |
| 32 | otherSize | trailing metadata (process name etc.) |
| 36 | (padding/reserved) | |

## Register block (23 words, starts at file offset 40)

```
r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 sp lr pc cpsr dfsr ifsr far fpexc fpinst fpinst2
```

`dfsr`/`ifsr` are the ARM Data/Instruction Fault Status Registers — the low
nibble is the fault type. **`dfsr == 5`** (and `ifsr` with the same low
nibble) means *section translation fault*, i.e. a straightforward null/wild
pointer dereference (page/section simply isn't mapped) — by far the most
common 3DS homebrew crash. `far` is the actual faulting address; for a
"dereference field at offset N of a null-ish pointer" bug, `far` is usually
a small number close to N (e.g. `far=0x34` with a struct field at `+0x30`
means the base pointer was `0x4`, not exactly `nil`/`0` — this happens when
a non-polymorphic base class sits at a nonzero offset inside a polymorphic
derived class, and the compiler computes `base = derived + offset` before
checking `derived` for null).

## Code and stack blocks

Right after the register block: `codeDumpSize` bytes of raw code ending
exactly at `pc` (i.e. covering `[pc - codeDumpSize, pc)` — useful for a
manual disassembly sanity check without the elf), then `stackDumpSize` bytes
starting at `sp`. The stack block is your call-stack source: scan it for
words that look like text-segment addresses (compare against the `.elf`'s
`.text` VA range) — each such word found where a return address would sit
on an AAPCS stack frame is a caller-frame candidate. **Not every matching
word is a live frame** — stale/leftover stack data from a previous deeper
call can produce false-positive "frames"; cross-check candidates against the
actual call graph (does the symbol at that address really call the next
candidate down?) before treating a stack scan as a trustworthy backtrace.

## Symbolizing

```sh
export DEVKITPRO=/home/max/lcs_3ds/sdk-r55 DEVKITARM=$DEVKITPRO/devkitARM
export PATH="$DEVKITARM/bin:$PATH"
arm-none-eabi-addr2line -f -C -e stories/build/relcs.elf 0x002e0350
```

For the instruction actually at `pc` (not just the source line), disassemble
around it and cross-check against the register values — e.g. `vldr s18,
[r0, #48]` with `r0` known to be a near-null value directly confirms which
struct field access faulted, which is often more conclusive than the
addr2line source line alone (especially with inlining).

To find which C++ call site put a given return address on the stack (an
`lr` value), the same `addr2line -f -C -e <elf> <addr>` on that word works —
but remember dispatcher-style code (e.g. a big `if/else if` chain calling
different handler functions from one call site) means a single `lr` address
can correspond to several different logical call sites; use the surrounding
disassembly (what was in `r0`-`r3` / on the stack) to disambiguate which
one actually ran.

## Common mistake

Treating every word in the stack dump that happens to fall in `.text` as a
real stack frame. Build the "live" call chain first from the function that
directly called the faulting one (verified via that function's own
disassembly actually branching to the crash-site function), then treat
everything else in the stack dump as unverified/possibly-stale until each
candidate frame is independently confirmed the same way.
