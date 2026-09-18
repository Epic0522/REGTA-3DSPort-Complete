<img src="logo.png" alt="re3 logo" width="200">

# GTA III for New Nintendo 3DS

This is the GTA III port in REGTA, based on re3 and the community's Nintendo
3DS port. It keeps Liberty City's original missions and gameplay, with a
lower-screen map, Nintendo controls and changes to help it run on New 3DS.

You need a New Nintendo 3DS, New 3DS XL or New 2DS XL, and your own GTA III PC
game data. The original 3DS and 2DS are not supported. Full game data, compiled
builds and the compiler are not included; selected overrides and HOME artwork are.

See the [main README](../README.md) for shared dependencies and the full
[changelog](../README.md#changelog).

## What changed

- Full lower-screen interface in GTA III's dark-blue theme.
- Faster loading and smoother streaming.
- Removed motion-blur trails.
- Fixed vehicle polygons, highlights, windows, lights and decals.
- Fixed sunset colours overpowering vehicle paint.
- Fixed the tower-clock crash and radio-related gameplay freeze.
- Reduced costly effects in busy scenes.
- Nintendo controls and a text cheat keyboard.
- Full mission names in the save list.
- Final-mission music: 'push it to the limit'.

[Full changelog →](../README.md#grand-theft-auto-iii--re3)

## Build and install

Run these commands from the **repository root**, not from `III`.
Install the dependencies listed in the
[build guide](../README.md#building-from-source) first. The compiler must be
devkitARM r55 / GCC 10.2. On Linux and macOS, build the SDK using the
[r55 source-build guide](../README.md#linux-and-macos-build-r55-from-official-sources);
the old downloader cannot supply the required files. Then set your SDK paths:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/path/to/devkitARM-r55

./scripts/verify-layout.sh
./scripts/build.sh re3
```

This produces `III/build/re3.elf` and `III/build/re3.3dsx`.
Keep the `vendor` links to `../common` intact.

Prepare your PC data and install the executable:

```sh
./scripts/setup-game.sh re3 "/path/to/GTA III" "/Volumes/SD/3ds"
./scripts/install-3dsx.sh re3 "/Volumes/SD/3ds"
```

The setup helper applies the included overrides in `gamefiles/re3` at the
repository root; see the [data preparation guide](../README.md#preparing-game-data).
It preserves existing saves and does not modify your
original PC installation.

Your SD card should contain:

```text
sdmc:/3ds/re3.3dsx
sdmc:/3ds/re3/
sdmc:/3ds/re3/userfiles/
```

A CIA uses the same data directory. See [CIA packaging](../README.md#cia-packaging)
for the tools needed to build one with the included artwork.

## Controls

| Control | Action |
| --- | --- |
| Circle Pad | Move or steer |
| C-stick | Move the camera |
| START | Pause |
| SELECT | Cycle the gameplay camera |
| A / B in menus and Ammu-Nation | Confirm or buy / return or leave |
| R with the AK-47 or M16 | Third-person auto-aim |
| L + R with the AK-47 or M16 | First-person aim |
| A / B in the sniper scope | Zoom in / out |
| L + R + ZL + ZR during gameplay | Open the text cheat keyboard |

In Standard mode, entering first-person rifle aim suppresses L's fire action
until you release it, so the shortcut does not waste a shot.

Touch the lower screen to reveal L3, R3 and the Camera region. Release, then
tap a button or drag the camera. The overlay hides after five seconds of
inactivity. The keyboard accepts the game's text cheats.

## Data, audio and saves

GTA III uses `audio` in lowercase. The optional final-mission music files are:

```text
re3/audio/music/PUSH_FM.WAV
re3/audio/music/PUSH_LOOP.WAV
```

The music starts with gameplay after Claude strikes the guard, then switches
to the looping file when the opening finishes. It plays at 80% volume during
gameplay and 40% during scripted scenes. Vehicle radio switching is disabled
while the mission music is active.

Keep the generated `models/txd.img` and `models/txd.dir` texture cache.
Generating it on the console can take a while; removing a working cache means
doing that conversion again. Settings are stored in `re3.ini`, and saves
are in `userfiles`. Back up the save folder before testing modified scripts
or converting saves.

## Limitations and modding

Busy scenes can still cause frame drops. Texture, model and script mods must
fit the console's memory and the formats supported by the port. Desktop ASI
plugins, CLEO scripts and binary patches do not work; code changes need to be
integrated into the source and rebuilt.

The inherited desktop build files are still present, but the instructions here
describe the New 3DS build.

## Credits

Based on re3 by aap and the re3 contributors, the community Nintendo 3DS port,
and the shared libraries listed in the [main README](../README.md#credits-and-legal-notice).

This project is not affiliated with Rockstar Games or Take-Two Interactive.
The upstream code is provided for educational, documentation and modding
purposes. Preserve upstream credits and keep derivative source available.
