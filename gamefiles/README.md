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
