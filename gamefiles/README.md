# Runtime overrides

This folder contains only the files applied by `scripts/setup-game.sh`.
The rest of each game's data must come from your own copy of the game.

| Folder | SD destination | Contents |
| --- | --- | --- |
| `re3/` | `/3ds/re3/` | Updated English GXT, particle settings, debug/free-roam scripts, final-mission music and startup movies |
| `revc/` | `/3ds/miami/` | Updated English GXT, particle settings, final-mission music and startup movies |
| `relcs/` | `/3ds/relcs/` | Final-mission music, startup movies and the loading-screen TXD |

The original game models, radio stations, world data and other language files
are not included here. LCS text changes are handled in code.

The installer copies a fixed list rather than the whole folder. Keep that list
and the exact file exceptions in the root `.gitignore` in sync when adding
an override.

The original source installation and existing destination saves are preserved.

The 3DS executable checks these overrides by exact size and SHA-256 at startup.
Missing or mismatched files stop loading and show the affected path; details also go to
`regta-install-check.log` in the game's data folder. Original core archives are
checked for presence, not against a particular PC/PS2 release's checksum.
Player-converted VC/LCS audio, saves, settings, other language files and optional
generated caches are excluded from exact hashes; converter output may vary while
remaining usable.
Replacing a checked override (including `american.gxt`) requires rebuilding its
manifest with `python3 scripts/tools/generate_install_manifest.py` and rebuilding
the executable. `scripts/verify-layout.sh` checks that the manifest is current.
