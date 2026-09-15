<img src="logo.png" alt="reLCS logo" width="200">

# reLCS for New Nintendo 3DS

Based on [reStories by knackers4 and its contributors](https://github.com/knackers4/res).

**The main story has been completed on a physical New Nintendo 3DS.**
Side missions and optional activities are not yet verified.

Requires a New Nintendo 3DS, New 3DS XL or New 2DS XL and converted **PS2 LCS
data**. Old 3DS systems are not supported. Build the executable yourself;
full game data and the compiler are not included.

[Shared build guide](../README.md#building-from-source) ·
[Full changelog](../README.md#grand-theft-auto-liberty-city-stories--relcs)

## Runtime layout

The executable and original game data are installed separately:

```text
sdmc:/3ds/relcs.3dsx          Homebrew Launcher executable
sdmc:/3ds/relcs/              extracted and prepared LCS data
sdmc:/3ds/relcs/userfiles/    settings and save files
```

The CIA uses the same `sdmc:/3ds/relcs/` data directory. Installing a CIA does
not install the original models, map, scripts or audio.

Older development builds used `sdmc:/3ds/restories/`. Move its `userfiles`
folder into `sdmc:/3ds/relcs/` before retiring the old directory.

## Preparing PS2 game data

Start with your own PS2 copy and the upstream
[reLCS Asset Converter](https://github.com/knackers4/res/releases/tag/relcs).
The reStories release provides it as `reLCSAssetConverter.exe`, a Windows
tool for converting the PS2 assets into the layout reLCS expects. Follow the
[upstream instructions](https://github.com/knackers4/res#how-can-i-try-it)
for that step; simply extracting the ISO is not the same as converting its
assets.

Use the repository-level setup helper from the root of
REGTA-3DSPort-Complete:

```sh
./scripts/setup-game.sh relcs "/path/to/extracted/LCS" "/Volumes/SD/3ds"
```

With no arguments, `setup-game.sh` also provides an interactive prompt.
The final argument must be the mounted card's `/3ds` directory, not the volume
root.

The prepared PS2 data directory must contain at least:

```text
DATA/gta_lcs.DAT
models/gta3.img
AUDIO/sfx.RAW
AUDIO/MUSIC/*.VB
AUDIO/NEWS/*.VB
AUDIO/CUTSCENE/*.VB
```

The setup helper checks and copies your PS2 data, then applies selected files
from the included `gamefiles/relcs` folder at the repository root:
`AUDIO/MUSIC`, `movies` and `txd/LOADSC0.TXD`.
See the [data guide](../README.md#preparing-game-data) for details.
Your original files and existing destination saves are preserved.

### Audio conversion

PS2 stream files are not copied directly to the SD card. The setup helper builds
the included VB decoder and uses `ffmpeg` to create formats chosen for New 3DS:

- MUSIC: 24 kHz mono IMA ADPCM WAV;
- NEWS: 24 kHz mono MP3;
- CUTSCENE: 24 kHz mono MP3;
- gameplay effects and speech: merged `sfx.RAW` plus `sfx.sdt`.

The source VB streams and split sound banks are not needed on the SD card
after conversion.

Required host tools for data preparation:

- a POSIX-compatible shell;
- `rsync`;
- a C++ compiler;
- `ffmpeg`.

## Building reLCS

Install the [shared build dependencies](../README.md#building-from-source)
first, including a separately downloaded devkitARM r55 / GCC 10.2. Then run
these commands from the repository root:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/path/to/devkitARM-r55

./scripts/verify-layout.sh
./scripts/build.sh relcs
```

Outputs:

```text
stories/build/relcs.elf
stories/build/relcs.3dsx
```

Use devkitARM r55 / GCC 10.2; newer versions can produce builds that crash.

To install the freshly built 3DSX:

```sh
./scripts/install-3dsx.sh relcs "/Volumes/SD/3ds"
```

This copies `stories/build/relcs.3dsx` to `/3ds/relcs.3dsx` without replacing
the prepared data or save directory.

## CIA package metadata

The [CIA packager](../README.md#cia-packaging) supports the custom icon,
animated Leone Sentinel banner and banner audio. The finished CGFX, encoded
audio and icon are included in `packaging/prebuilt`.

| Field | Value |
| --- | --- |
| Short title | `GTALCS For Nintendo 3DS` |
| Long title | `Grand Theft Auto: Liberty City Stories` |
| Title ID | `00040000002F6200` |
| Product code | `CTR-P-RLCS` |
| Data directory | `sdmc:/3ds/relcs/` |

The CIA requires the same SD game data as the 3DSX.

## Nintendo 3DS controls

The prompts use Nintendo button names. A confirms or buys in menus and shops;
B returns or leaves. Weapon and vehicle actions depend on what Toni is doing.

### On foot

| Control | Action |
| --- | --- |
| Circle Pad | Move Toni |
| C-stick | Move the camera |
| A | Sprint; zoom in in supported weapon sights |
| B | Jump; zoom out in supported weapon sights |
| Y | Enter a vehicle or perform the current vehicle interaction |
| X | Fire or use the equipped weapon |
| R | Aim; rifles use third-person auto-aim |
| L + R | First-person rifle aim |
| ZL / ZR | Cycle weapons; change target while locked on where supported |
| L | Re-centre the camera |
| R3 touch button | Look behind |
| START | Pause |
| SELECT | Cycle the gameplay camera |

### In a vehicle

| Control | Action |
| --- | --- |
| Circle Pad left / right | Steer |
| A | Accelerate |
| B | Brake or reverse |
| Y | Exit the vehicle |
| R | Handbrake |
| X | Fire the vehicle weapon when available |
| L | Change radio station |
| ZL / ZR | Look left / right |
| ZL + ZR | Look behind |
| L3 touch button | Horn or the scripted L3 action |
| C-stick | Move the camera |
| START | Pause |
| SELECT | Cycle the vehicle camera |

### Lower-screen touch controls

Touch to reveal the overlay, then release. Tap L3/R3 or drag the Camera region.
The overlay hides after five seconds of inactivity.

### Pause map

| Control | Action |
| --- | --- |
| Y | Place or remove a marker |
| ZR / R | Zoom |
| L | Toggle the legend |
| B | Return |

## 3DS text cheat keyboard

During gameplay, hold:

```text
L + R + ZL + ZR
```

Enter a code without spaces. Codes are case-insensitive. The keyboard is only
available during gameplay.

> [!WARNING]
> Cheats can change statistics, world behaviour and save-game state. The game
> may mark the session as cheated or show its normal save warning. Keep a clean
> save backup when testing cheats.

The main codes below are the GTA-style phrases written for this port. The short
aliases still work and trigger the same effects. Enter either one without spaces.

### Player, weapons and wanted level

| Cheat code | Short alias | Effect |
| --- | --- | --- |
| `STREETWISE` | `WEAPONS1` | Give weapon set 1 |
| `BUSINESSASUSUAL` | `WEAPONS2` | Give weapon set 2 |
| `MADEMANARSENAL` | `WEAPONS3` | Give weapon set 3 |
| `FAMILYFORTUNE` | `MONEY` | Add $250,000 |
| `TOUGHASTHEYCOME` | `RECOVER` / `HEALTH` | Restore player health and repair the current vehicle where supported |
| `SUITEDANDBOOTED` | `ARMOR` / `ARMOUR` | Restore armour |
| `COMEANDGETME` | `WANTEDUP` | Raise the wanted level by two stars, up to six |
| `FORGETABOUTIT` | `WANTEDOFF` | Clear the wanted level |
| `TONISLASTSTAND` | `SUICIDE` | Kill the player character |
| `NEWFACEINTOWN` | `PEDSKIN` | Change Toni to a random pedestrian model when possible |

### Navigation and debug helpers

| Cheat code | Short alias | Effect |
| --- | --- | --- |
| `TELEPORTH` | — | Teleport near the exterior entrance of the current island's unlocked safehouse |
| `TELEPORTM` | — | Teleport near the closest currently available unfinished main-story mission; side activities are excluded |
| `SKIP` | — | Arm the final mission’s boat checkpoint before starting the mission; unavailable during an active mission |

Teleports place you outdoors, away from the destination's trigger.

### Weather and time

| Cheat code | Short alias | Effect |
| --- | --- | --- |
| `HERECOMESTHESUN` | `SUNNY` | Force extra-sunny weather |
| `NOTACLOUDINSIGHT` | `CLEAR` | Force normal sunny weather |
| `CLOUDSOVERPORTLAND` | `CLOUDY` | Force cloudy weather |
| `RAININGONMYPARADE` | `RAIN` | Force rainy weather |
| `CANTFINDMYWAY` | `FOG` | Force foggy weather |
| `TIMENEVERWAITS` | `FASTCLOCK` | Toggle the accelerated world clock/weather cycle |
| `LIFEINTHEFASTLANE` | `FASTGAME` | Increase game time scale, up to the supported maximum |
| `SLOWANDSTEADY` | `SLOWGAME` | Decrease game time scale, down to the supported minimum |

### Vehicles and traffic

| Cheat code | Short alias | Effect |
| --- | --- | --- |
| `TANKYOULIBERTY` | `RHINO` | Spawn a Rhino |
| `TAKEOUTTHETRASH` | `TRASHMASTER` | Spawn a Trashmaster |
| `BOOMTOWN` | `BLOWUP` | Destroy all currently loaded vehicles |
| `ALLLIGHTSGREEN` | `GREENTRAFFIC` | Force green traffic lights |
| `ROADRAGE` | `AGGRESSIVEDRIVERS` | Enable aggressive traffic behaviour |
| `BACKINBLACK` | `BLACKCARS` | Force black traffic-car colours |
| `WHITEWASH` | `WHITECARS` | Force white traffic-car colours |
| `BLINGBLING` | `CHROMECARS` | Force chrome traffic-car colours |
| `FLOATMYBOAT` | `DRIVEONWATER` | Toggle the hover/drive-on-water vehicle behaviour |
| `HANDLEWITHCARE` | `HANDLING` | Toggle enhanced handling; this does not prevent rollovers |
| `WHEELDEAL` | `BIKETIRES` | Toggle the LCS bike-tire cheat |

### Pedestrians, display and special modes

| Cheat code | Short alias | Effect |
| --- | --- | --- |
| `CITYGONECRAZY` | `RIOT` | Enable pedestrian riot/mayhem behaviour |
| `EVERYBODYHATESME` | `ATTACKME` | Make pedestrians treat the player as a threat |
| `ARMEDANDDANGEROUS` | `PEDWEAPONS` | Toggle weapons for pedestrians |
| `BIGHEADS` | — | Debug entry; the big-head effect does not work in this build |
| `BOYSCLUB` | `FOLLOWME` | Toggle male pedestrian followers |
| `HOPINPAL` | `PASSENGER` | Invite a nearby pedestrian into the current car or bike |
| `FIFTEENMINUTES` | `MEDIA` | Show the chase/media statistic overlay |
| `CREDITS` | — | Debug entry; does not start the credits in this build |
| `TOPSYTURVY` | `UPSIDEDOWN` | Enable the LCS upside-down camera/display mode |
| `RIGHTSIDEUP` | `REVERSEUPSIDE` | Disable the upside-down camera/display mode |

`BIGHEADS` and `CREDITS` are non-working debug entries and have no longer aliases.
This does not affect the normal ending credits. `TELEPORTH`, `TELEPORTM` and
`SKIP` keep their original names.

## What changed

- Completed and repaired reStories so the main story can be finished on New 3DS.
- Full lower-screen interface in LCS's red theme.
- Fixed progression, saved world changes and special garage vehicles.
- Fixed the final mission's boats, helicopter combat and boarding sequence.
- Restored flamethrower damage in 'Friggin’ the Riggin’'.
- Fixed glass-shattering crashes and restored the crusher magnet.
- Fixed cutscene characters, vehicle occupants, arrests and landing animations.
- Restored race countdowns and removed broken checkpoint light columns.
- Fixed water, ferry materials, foliage and distant-island LODs.
- Added lightweight vehicle reflections and fixed windows, decals and plates.
- Faster streaming and lower memory use.
- Full save titles and a three-second minimum mission-title display.
- Restored ending credits and added final-mission music.

[Full changelog →](../README.md#grand-theft-auto-liberty-city-stories--relcs)

## Final-mission music

The edited 'Chase' tracks belong at:

```text
relcs/AUDIO/MUSIC/CHASE_FM.WAV
relcs/AUDIO/MUSIC/CHASE_LOOP.WAV
```

The opening starts with the first gameplay objective, or at the boat checkpoint
when restarting through the hospital taxi. When it finishes, the loop begins.
The confrontation with Massimo fades the current music to silence, then the
helicopter objective starts the edited climax once. Skipping the scene also
switches to the climax. The helicopter crash fades and stops the track.

Ordinary cutscenes lower the music to 50% without pausing it. Radio switching
is disabled during the mission, and the song name is shown while driving.

## Texture conversion and performance

The 3DS renderer uses a native `models/txd.img`/`models/txd.dir` cache. If the
cache is not present, first launch may spend a long time converting textures. Do not remove
a known-good cache merely because the original TXD files remain beside it.

Dense vegetation, traffic and complex cutscenes can still reduce frame rate.

## Save data

Native 3DS saves are stored in:

```text
sdmc:/3ds/relcs/userfiles/
```

Back up this folder before changing scripts or converting saves. PS2 saves need
conversion; renaming a file is not enough.

## Known limitations

- Main-story completion has been verified on real hardware; side missions and
  other optional activities have not yet been verified.
- New 3DS-family hardware only.
- The initial native texture build can be slow.
- Dense vegetation and complex real-time cutscenes can still reduce frame rate.
- The pause map and local radar can differ slightly in alignment.
- Desktop ASI/CLEO plugins and binary patches are not compatible.
- Some unused upstream features and debug cheats remain unimplemented.

## Reporting bugs

Please open an Issue with:

- Mission/location and steps to reproduce the problem.
- Build or commit, console model, and CIA or 3DSX.
- Expected and actual behaviour; whether restarting helps.
- Any cheats or mods used.
- Crash dump, screenshots/video and a save before the problem, when available.

Freezes and gameplay bugs can be reported without a crash dump.

## Source layout

Important paths inside this game tree:

| Path | Purpose |
| --- | --- |
| `src/` | reLCS game code and game-specific 3DS integration |
| `build/GNUmakefile` | pinned New 3DS build entry point |
| `vendor/` | symbolic links to the unified shared dependencies |
| `../gamefiles/relcs/` | selected runtime overrides installed by setup |
| `tools/lcs_vb_decode.cpp` | host decoder for PS2 VB streams |
| `tools/convert_lcs_music_adpcm_3ds.sh` | MUSIC conversion route |
| `tools/convert_lcs_streams_3ds.sh` | NEWS and CUTSCENE conversion route |

## Credits and legal notice

The LCS code is based on
[reStories (`knackers4/res`)](https://github.com/knackers4/res), which also
provides the [PS2 asset converter](https://github.com/knackers4/res/releases/tag/relcs).
Thanks to knackers4 and the reStories contributors for that foundation.

This port also builds on re3/reVC, the community Nintendo 3DS port, librw,
devkitPro, libctru, Citro3D, OpenAL Soft, mpg123 and their contributors.

The code is intended for educational, documentation and modding purposes. It
does not distribute the original game and does not encourage piracy or
commercial use. Preserve upstream credit and keep derivative source available.
