<img src="logo.png" alt="reVC logo" width="200">

# Vice City for New Nintendo 3DS

This is REGTA's Vice City port, based on reVC and the community's Nintendo 3DS
port. It adds a pink lower-screen map and HUD, Nintendo controls, faster
loading and fixes for the rendering and audio problems found on 3DS.

You need a New Nintendo 3DS, New 3DS XL or New 2DS XL, and your own Vice City
PC data. The original 3DS and 2DS are not supported. Full game data, compiled
builds and the compiler are not included; selected overrides and HOME artwork are.

The folder is still called `miami`, as in upstream reVC. It also remains the
SD data folder, so older installations do not need to move their game files.

See the [main README](../README.md) for shared dependencies and the full
[changelog](../README.md#changelog).

## What changed

- Full lower-screen interface in Vice City's pink theme.
- Much faster animation and collision loading.
- Restored particles and transparency.
- Fixed broken character polygons and vehicle materials.
- Fixed overlapping decals and plates.
- Smoothed water transitions; fixed missing and white water sections.
- Fixed flight-related crashes and textures staying blurry.
- Reduced vehicle-highlight brightness.
- Fixed stale textures in the loading dialog.
- Nintendo controls and a text cheat keyboard.
- Full mission names in the save list.
- Final-mission music: 'Self Control', with a separate Lance climax.

[Full changelog →](../README.md#grand-theft-auto-vice-city--revc)

## Build and install

Run these commands from the **repository root**, not from `miami`.
Install the [build dependencies](../README.md#building-from-source), including
devkitARM r55 / GCC 10.2. The
[Linux and macOS source-build guide](../README.md#linux-and-macos-build-r55-from-official-sources)
builds the required SDK from official sources on either OS, without the old
downloader. Set your SDK paths:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/path/to/devkitARM-r55

./scripts/verify-layout.sh
./scripts/build.sh revc
```

The outputs are `miami/build/miami.elf` and `miami/build/miami.3dsx`.
The install helper renames the executable to `revc.3dsx`. The build helper
sets the loading, lower-screen and `OPTIMIZED_BUILD=1` options.

```sh
./scripts/setup-game.sh revc "/path/to/Vice City" "/Volumes/SD/3ds"
./scripts/install-3dsx.sh revc "/Volumes/SD/3ds"
```

Setup applies the included `gamefiles/revc` overrides from the repository
root, listed in the [data preparation guide](../README.md#preparing-game-data).
The helper copies your PC data without modifying
the original installation and preserves existing destination saves.

The resulting SD layout is:

```text
sdmc:/3ds/revc.3dsx
sdmc:/3ds/miami/
sdmc:/3ds/miami/userfiles/
```

CIA builds use the same `miami` data folder. Packaging instructions and
the included finished artwork are described in the
[CIA guide](../README.md#cia-packaging). Keep the game's `vendor` links
to `../common` intact when copying the source.

## Controls

| Control | Action |
| --- | --- |
| Circle Pad | Move or steer |
| C-stick | Move the camera |
| START | Pause |
| SELECT | Cycle the gameplay camera |
| A / B in menus and shops | Confirm or buy / return or leave |
| R with the Ruger, M4 or M60 | Third-person auto-aim |
| L + R with those weapons | First-person aim |
| A / B in the sniper scope | Zoom in / out |
| Y on the pause map | Place or remove a marker |
| ZR / R on the pause map | Zoom |
| L on the pause map | Toggle the legend |
| B on the pause map | Return |
| L + R + ZL + ZR during gameplay | Open the text cheat keyboard |

Both Ammu-Nation and the hardware store use B to leave. In Standard mode,
entering first-person rifle aim suppresses L's fire action until release.

Touch the lower screen once to reveal the L3, R3 and Camera regions. Release,
then tap or drag. The overlay hides after five seconds without touch input.
The keyboard accepts Vice City's text cheats.

## Continuous audio

Setup converts the radio stations and long ambience tracks to 24 kHz mono IMA
ADPCM, as in LCS. This reduces the MP3 startup load when entering the hotel,
changing ambience or tuning the radio. Python 3 and FFmpeg are required. To
update an existing installation, run:

```sh
python3 miami/tools/convert_vc_radio_3ds.py "/path/to/Vice City/Audio" "/path/to/radio-output"
```

Copy the generated WAV files into `sdmc:/3ds/miami/Audio/` and use the updated
executable. Original ADF and MP3 files can stay; they are used when a converted
WAV is missing. The converter does not change the source files, mission dialogue
or final-mission music.

FFmpeg may print `Error submitting packet to decoder: Invalid data found when
processing input` once for each original ADF stream. The stock Vice City ADF
files contain a packet FFmpeg rejects, then conversion continues normally. If
setup reaches `Prepared revc data` and the generated WAV files play normally,
the warning is harmless. `The original game directory was not changed` only
confirms that setup wrote the prepared files to the SD directory without
modifying the source installation.

## Final-mission music

Setup installs the edited tracks at:

```text
miami/Audio/music/SELF_FM.WAV
miami/Audio/music/SELF_LOOP.WAV
```

'Self Control' starts when gameplay begins in `Keep Your Friends Close...`.
The opening plays into the loop. Lance's reveal fades the current section to
silence; the following gameplay objective starts the climax once, even if the
objective text appears again. Skipping the scene also switches to the climax.
The ending fades and stops the music.

The song title appears briefly at mission start. Gameplay volume is 80%, with
scripted scenes at 40%, and radio switching is disabled during the mission.

## Textures and saves

Keep `models/txd.img` and `models/txd.dir` once the native texture cache
has been generated. First-time conversion can be slow, but the cache helps
subsequent loading and streaming.

Settings are stored in `reVC.ini`; saves are in `userfiles`. Back up that
folder before replacing scripts or experimenting with save conversion. Preserve
the documented `Audio` directory case when preparing files on your computer.

## Limitations and modding

Dense scenes and demanding cutscenes can still cause frame drops or audio
stutter. Particle limits reduce rendering cost without changing weapon damage
or hit detection.

Desktop ASI plugins, CLEO scripts and binary patches do not work. Model,
texture and script changes must fit the port's formats and memory limits;
code changes need a rebuild. The inherited desktop build files are present,
but this guide covers New 3DS.

## Credits

Based on reVC by aap and the re3/reVC contributors, the community Nintendo 3DS
port, and the shared libraries listed in the
[main README](../README.md#credits-and-legal-notice).

This project is not affiliated with Rockstar Games or Take-Two Interactive.
The upstream code is provided for educational, documentation and modding
purposes. Preserve upstream credits and keep derivative source available.
