# Building CIA packages

Build all three game ELFs first, then run this from the repository root:

```sh
./packaging/production_cia/build_production.sh
```

Install zsh, bannertool, makerom and 3dsxtool separately. The script looks for
the three tools on PATH; override their paths with BANNERTOOL, MAKEROM and
THREEDSXTOOL if needed.

It reads the finished CGFX banners, encoded audio and 48×48 icons from
[../prebuilt](../prebuilt/README.md). No Blender, pycgfx, ImageMagick or original
vehicle assets are needed. Banner audio is copied without re-encoding, and the
three rebuilt BNRs match the accepted banners byte-for-byte.

ELF inputs:

- `III/build/re3.elf`
- `miami/build/miami.elf`
- `stories/build/relcs.elf`

CIA and icon-equipped 3DSX files are written to `output/`, together with
intermediate BNR and SMDH files. That directory is ignored by Git.
Set `OUTPUT_DIR` to use a temporary output directory for verification.

| Game | Title ID | Product code | Banner vehicle |
| --- | --- | --- | --- |
| GTA III | `00040000002F6000` | `CTR-P-0RE3` | Kuruma |
| Vice City | `00040000002F6100` | `CTR-P-REVC` | Admiral |
| Liberty City Stories | `00040000002F6200` | `CTR-P-RLCS` | Leone Sentinel |

All three use publisher `Epic`, New 3DS 804 MHz mode, L2 cache, expanded
application memory, direct SDMC access and the MVD video service. The long HOME
titles are the full game names. The packages contain no game-data RomFS:
install the data separately under `/3ds/re3`, `/3ds/miami` or `/3ds/relcs`.

The full CIA hash can change with generated ticket/TMD metadata. Do not use
whole-package hashes alone to decide whether the game or banner has changed.
