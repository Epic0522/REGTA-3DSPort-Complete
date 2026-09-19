<p align="center">
  <img src="logo.png" alt="REGTA logo" width="280">
</p>

<p align="center">
  <a href="III/README.md"><img src="III/logo.png" alt="re3 — GTA III" width="150"></a>
  <a href="miami/README.md"><img src="miami/logo.png" alt="reVC — Vice City" width="150"></a>
  <a href="stories/README.md"><img src="stories/logo.png" alt="reLCS — Liberty City Stories" width="150"></a>
</p>

# REGTA-3DSPort-Complete

REGTA brings three classic Grand Theft Auto games to New Nintendo 3DS:

- **Grand Theft Auto III**, based on re3 (`III`)
- **Grand Theft Auto: Vice City**, based on reVC (`miami`)
- **Grand Theft Auto: Liberty City Stories**, based on
  [reStories/reLCS](https://github.com/knackers4/res) (`stories`)

Trailer：https://youtu.be/fBzzLx0BX5M

Build any of the three games from this repository. Each has a full lower-screen
interface, touch controls, Nintendo button prompts and a cheat-code keyboard.

> [!IMPORTANT]
> Build the ports yourself and provide your own game data: PC GTA III or Vice
> City, or converted PS2 Liberty City Stories. Full game data, executables and
> the devkitARM toolchain are not included. Runtime overrides and finished HOME
> Menu artwork are included.

## The games today

### GTA III

- Dark-blue lower-screen interface.
- Faster loading and smoother streaming.
- Removed motion blur; fixed vehicle materials and several crashes.
- Final-mission music: 'push it to the limit'.

Tested on New Nintendo 3DS. Busy scenes can still drop frames.
Uses **PC GTA III data**. [Setup and controls →](III/README.md)

### Vice City

- Pink lower-screen interface.
- Faster loading; restored particles and vehicle highlights.
- Fixed character and vehicle polygons, water seams and flight-related crashes.
- Final-mission music: 'Self Control'.

Tested on New Nintendo 3DS. Demanding scenes can still cause frame drops or
audio stutter. Uses **PC Vice City data**. [Setup and controls →](miami/README.md)

### Liberty City Stories

- Completed the unfinished [reStories/reLCS](https://github.com/knackers4/res)
  campaign implementation for New Nintendo 3DS.
- Red lower-screen interface and lightweight vehicle reflections.
- Fixed missions, saves, world changes, cutscenes and vehicle bugs.
- Custom text cheats and final-mission music: 'Chase'.

**Main story completed on a physical New Nintendo 3DS.** Side missions and
other optional activities are not yet verified. Please [report problems](#reporting-bugs)
with reproduction steps and any screenshots, video or crash dump.

Uses **converted PS2 LCS data**. [Setup, controls and cheats →](stories/README.md)

## Supported hardware

- New Nintendo 3DS
- New Nintendo 3DS XL
- New Nintendo 2DS XL
- A homebrew-capable system for 3DSX builds, or custom firmware for CIA builds

Old Nintendo 3DS, Old Nintendo 3DS XL and Nintendo 2DS systems are not
supported. These ports use the New 3DS's faster CPU, L2 cache and extra memory.

Before gameplay, the renderer requires the New 3DS MVD service to prepare its
neutral material texture. The check runs late in game loading, after TXD index
preparation and animation/model loading (around the 80% stage in VC). Startup
and menu rendering use a temporary neutral texture so existing game fonts are
available for the failure message. Skipping movies or using cached TXDs does
not bypass the check. Without working conversion, both screens turn black and
the upper screen displays "Device not supported" in the normal help-text font.
B exits, and HOME remains handled by the normal APT loop.
This new renderer path is experimental and still needs physical-console
validation, including both CIA and 3DSX service access.

## Project layout

All three games and their shared dependencies live in this repository.
Download it once, then choose which game to build; there are no separate game
repositories or branches to assemble.

```text
REGTA-3DSPort-Complete/
├── common/       shared 3DS renderer, audio and platform dependencies
├── III/          GTA III / re3 game tree
├── miami/        Vice City / reVC game tree
├── stories/      Liberty City Stories / reLCS game tree
├── gamefiles/    selected runtime overrides for the three games
├── scripts/      setup, build, layout-check and 3DSX install helpers
├── packaging/    CIA scripts and finished CGFX banners, audio and icons
├── tools/        shared host-side utilities
└── docs/         historical implementation and verification notes
```

Keep the repository layout and symbolic links intact, even when building only
one game. Each game's `vendor` folder links to the shared libraries in `common`.

### The three game trees

| Source tree | Game | Original data source | SD data path | 3DSX filename |
| --- | --- | --- | --- | --- |
| `III` | Grand Theft Auto III | PC | `sdmc:/3ds/re3/` | `re3.3dsx` |
| `miami` | Grand Theft Auto: Vice City | PC | `sdmc:/3ds/miami/` | `revc.3dsx` |
| `stories` | Grand Theft Auto: Liberty City Stories | PS2 | `sdmc:/3ds/relcs/` | `relcs.3dsx` |

Vice City uses `/3ds/miami` for data and `revc.3dsx` for the executable.
Older reLCS builds may have used `/3ds/restories`; move that `userfiles` folder
to `/3ds/relcs` before removing an old installation.

## Quick start

Install the [build dependencies](#host-prerequisites) first.
Then, from the repository root:

```sh
./scripts/setup-game.sh relcs "/path/to/extracted/LCS/PS2/data" "/Volumes/SD/3ds"
./scripts/build.sh relcs
./scripts/install-3dsx.sh relcs "/Volumes/SD/3ds"
```

Replace `relcs` with `re3` or `revc` and provide the corresponding PC game
directory when preparing either PC title.

Installing the executable does not install the game data. Both CIA and 3DSX
builds load it from the matching folder under `sdmc:/3ds/`.

## Preparing game data

Run the interactive helper with no arguments:

```sh
./scripts/setup-game.sh
```

It asks for:

1. `re3`, `revc` or `relcs`;
2. the original game-data directory;
3. the mounted SD card's `/3ds` directory, not the SD root.

The same operation can be scripted:

```sh
./scripts/setup-game.sh re3   "/path/to/GTA III"       "/Volumes/SD/3ds"
./scripts/setup-game.sh revc  "/path/to/Vice City"     "/Volumes/SD/3ds"
./scripts/setup-game.sh relcs "/path/to/extracted LCS" "/Volumes/SD/3ds"
```

The helper copies the required data without changing the original files. It
leaves out desktop executables and temporary files, then copies selected
overrides from the root `gamefiles/<game>` folder. Existing saves in the
destination's `userfiles` folder are preserved.

See [gamefiles](gamefiles/README.md) for the included overrides. Original models,
radio stations and other base data must come from your own game.

### Expected SD data layout

This layout matches the files used for testing on a physical console:

```text
sdmc:/3ds/
├── re3/
│   ├── TEXT/  anim/  audio/music/  data/  models/  movies/  mp3/  txd/
│   ├── models/txd.img  models/txd.dir
│   ├── re3.ini
│   └── userfiles/
├── miami/
│   ├── Audio/music/  TEXT/  anim/  data/  models/  movies/  mp3/  txd/
│   ├── models/txd.img  models/txd.dir
│   ├── reVC.ini
│   └── userfiles/
└── relcs/
    ├── AUDIO/{CUTSCENE,MUSIC,NEWS}/  DATA/  TEXT/  anim/  models/
    ├── movies/  neo/  txd/
    ├── models/txd.img  models/txd.dir
    ├── reLCS.ini
    └── userfiles/
```

Keep the documented letter case even if the SD card itself is case-insensitive:
staging and verification may occur on a case-sensitive host. GTA III uses
`audio`, Vice City uses `Audio`, and LCS uses `AUDIO`; LCS data uses `DATA`.
The final-mission tracks therefore live at:

```text
re3/audio/music/PUSH_FM.WAV        re3/audio/music/PUSH_LOOP.WAV
miami/Audio/music/SELF_FM.WAV      miami/Audio/music/SELF_LOOP.WAV
relcs/AUDIO/MUSIC/CHASE_FM.WAV     relcs/AUDIO/MUSIC/CHASE_LOOP.WAV
```

The setup helper copies these included overrides:

| Game | Included data overrides |
| --- | --- |
| GTA III | `TEXT/american.gxt`, `audio/music`, `data/PARTICLE.CFG`, `data/main_d.scm`, `data/main_freeroam.scm`, `movies` |
| Vice City | `Audio/music`, `TEXT/american.gxt`, `data/particle.cfg`, `movies` |
| LCS | `AUDIO/MUSIC`, `movies`, `txd/LOADSC0.TXD` |

The remaining files come from your original game data or are generated by the
port. If you add an override, update both this list and the setup helper.

A clean install creates default `.ini` settings on first launch. Keep your own
settings and saves in `userfiles`; there is no need to copy someone else's.
Keep the generated `models/txd.img` and `models/txd.dir` texture cache too—it
makes a substantial difference to loading and streaming speed.

Desktop DLLs, old executables, logs and converter utilities left over from an
earlier installation are not needed on the SD card.

### Liberty City Stories data and audio

Prepare your PS2 assets with the
[reLCS Asset Converter from reStories](https://github.com/knackers4/res/releases/tag/relcs)
first. Its Windows executable is named `reLCSAssetConverter.exe`; see the
[upstream instructions](https://github.com/knackers4/res#how-can-i-try-it).
REGTA's setup helper expects converted assets, not just the contents of an
extracted ISO.

Select the prepared PS2 data folder containing:

```text
DATA/gta_lcs.DAT
models/gta3.img
AUDIO/sfx.RAW
AUDIO/MUSIC/*.VB
AUDIO/NEWS/*.VB
AUDIO/CUTSCENE/*.VB
```

REGTA's audio-conversion step requires `ffmpeg` and a host C++ compiler. It converts the 19
continuous music/radio streams to 24 kHz mono IMA ADPCM WAV, avoiding the
real-time cost of MP3 radio decoding on 3DS. NEWS and CUTSCENE streams become
24 kHz mono MP3. The large source VB streams and redundant `SET0` through
`SET6` split banks are not copied; the active merged `sfx.RAW` and `sfx.sdt`
gameplay sound library is retained.

See [`stories/README.md`](stories/README.md) for the complete reLCS-specific
setup, controls, features and cheat-code reference.

## Building from source

### Host prerequisites

Production builds require devkitARM release 55 / GCC 10.2, which is not
distributed in this repository. On both Linux and macOS, build the toolchain
and devkitPro host tools from source as described below. The old binary
downloader returns 403 for the required historical files; a current devkitPro
installation is not a replacement for r55.
A newer system compiler can produce an executable that links cleanly but fails
on hardware because these ports use legacy libctru/newlib-era code.

Keep symbolic links intact when cloning or extracting the source. You will need:

- a POSIX shell, GNU Make, `rsync`, `md5sum` and normal Unix build tools;
- devkitPro host tools (bin2s, Picasso, 3dsxtool and smdhtool);
- a source-built devkitARM release 55 / GCC 10.2 compiler tree;
- `ffmpeg` for VC/LCS audio preparation, Python 3 for VC, and a host C++ compiler for LCS.

Command Line Tools and Homebrew on macOS, or the system packages on Linux,
provide the host build dependencies, not the required r55 SDK. After building
the SDK, point `DEVKITARM` at its compiler tree and `DEVKITPRO` at its root.
For example:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/path/to/devkitARM-r55
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
```

The build helper honors these variables and defaults to the conventional
`/opt/devkitpro` layout when they are not set.

### Linux and macOS: build r55 from official sources

Use this source-build route on both Linux and macOS.
Cloning the latest buildscripts is not enough: it builds a different compiler,
and historical scripts still try to download archives from the old server.
We pin the r55 scripts and prefetch their sources from GNU and Sourceware.
The versions, source scripts and URLs below were checked, but the full SDK
bootstrap has not yet been tested end-to-end here on either OS. Building the
games with an existing r55 SDK is not the same as testing an SDK bootstrap.
Allow several GB of disk space and use paths that contain no spaces.

#### 1. Host dependencies and working directories

**Linux (Ubuntu/Debian, x86-64):**

```sh
sudo apt-get update
sudo apt-get install build-essential git curl ca-certificates autoconf automake \
  libtool bison flex texinfo pkg-config libgmp-dev libmpfr-dev libmpc-dev \
  zlib1g-dev libncurses-dev libreadline-dev libexpat1-dev \
  xz-utils bzip2 rsync python3 ffmpeg
```

Other Linux distributions need the equivalent development packages.

**macOS:** install Xcode Command Line Tools and Homebrew first. The pinned
r55 scripts use Intel-era `/usr/local` include/library paths. The commands
below therefore target an Intel Mac, or an x86-64 shell under Rosetta with a
separate Intel Homebrew on Apple Silicon. Native arm64 SDK bootstrapping is
not covered; do not mix `/opt/homebrew` arm64 libraries into this build.

```sh
xcode-select --install
```

Wait for installation to finish (skip it if Command Line Tools are already
installed). On Apple Silicon, start a Rosetta shell with `arch -x86_64 /bin/bash`
after installing Rosetta and Intel Homebrew. In that shell, or on an Intel Mac:

```sh
test -x /usr/local/bin/brew
eval "$(/usr/local/bin/brew shellenv)"
brew install autoconf automake libtool bison flex texinfo pkg-config \
  gmp mpfr libmpc xz coreutils rsync python ffmpeg
export PATH="$(brew --prefix bison)/bin:$(brew --prefix flex)/bin:$(brew --prefix texinfo)/bin:$PATH"
export OSXSDKPATH="$(xcrun --show-sdk-path)"
```

Modern macOS SDKs/Clang may need further compatibility patches for these old
sources. This is an unverified bootstrap route, not a claim that r55 builds
unchanged on every macOS release. A Linux x86-64 VM is an alternative for
building both the SDK and games; its compiler binaries will not run on macOS.

**Both systems:** use the same shell for the remaining steps.

```sh
export REGTA_SDK="$HOME/devkitpro-r55"
export REGTA_SDK_SRC="$HOME/regta-sdk-source"
mkdir -p "$REGTA_SDK" "$REGTA_SDK_SRC/archives"
cd "$REGTA_SDK_SRC"
```

#### 2. Compiler, Binutils and Newlib

This commit identifies itself as r55 and selects GCC 10.2.0, Binutils 2.34
and Newlib 3.3.0. Do not substitute the `v20201105` tag: that is r54.

```sh
git clone https://github.com/devkitPro/buildscripts.git
git -C buildscripts checkout ef789eddb72421c1b3774767c02ee519cb38fc9e
cd "$REGTA_SDK_SRC/archives"
curl -fL --retry 3 -O https://ftp.gnu.org/gnu/gcc/gcc-10.2.0/gcc-10.2.0.tar.xz
curl -fL --retry 3 -O https://ftp.gnu.org/gnu/binutils/binutils-2.34.tar.xz
curl -fL --retry 3 -O https://sourceware.org/pub/newlib/newlib-3.3.0.tar.gz
xz -t gcc-10.2.0.tar.xz binutils-2.34.tar.xz
gzip -t newlib-3.3.0.tar.gz

cd "$REGTA_SDK_SRC/buildscripts"
cat > config.sh <<EOF
BUILD_DKPRO_PACKAGE=1
BUILD_DKPRO_INSTALLDIR="$REGTA_SDK"
BUILD_DKPRO_SRCDIR="$REGTA_SDK_SRC/archives"
BUILD_DKPRO_SKIP_CRTLS=1
BUILD_DKPRO_AUTOMATED=0
export MAKEFLAGS="-j2"
EOF
bash ./build-devkit.sh
```

`build-devkit.sh` fetches rules/crtls archives from `downloads.devkitpro.org`
with a plain `curl -f -L -O` and no User-Agent override. That host sits behind
Cloudflare and 403s curl's default User-Agent, which is the 403 referenced in
the troubleshooting note below. If you hit it, give curl a browser-like UA
before running the script:

```sh
echo 'user-agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36"' > "$HOME/.curlrc"
```

The scripts apply devkitARM's own patches; generic ARM GCC is not a substitute.
They reuse the three downloaded archives. Rules and startup objects are skipped
in this step and installed from Git below, avoiding the remaining old-server
downloads. Accept the displayed installation directory; answer **n** to the
final cleanup question if you want to keep the intermediate files. Do not build
as root. Keep `-j2` on a small VM; higher parallelism needs more RAM.

#### 3. Matching rules and startup objects

These old Makefiles use `/opt/devkitpro` inside `DESTDIR`. Stage their output
and copy it into the private SDK rather than overwriting a system installation.

```sh
export DEVKITPRO="$REGTA_SDK"
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
cd "$REGTA_SDK_SRC"
git clone --branch v1.2.0 --depth 1 https://github.com/devkitPro/devkitarm-rules.git
make -C devkitarm-rules install DESTDIR="$REGTA_SDK_SRC/rules-stage"
cp -a "$REGTA_SDK_SRC/rules-stage/opt/devkitpro/devkitARM/." "$DEVKITARM/"
git clone --branch v1.0.0 --depth 1 https://github.com/devkitPro/devkitarm-crtls.git
make -C devkitarm-crtls -j2
make -C devkitarm-crtls install DESTDIR="$REGTA_SDK_SRC/crtls-stage"
cp -a "$REGTA_SDK_SRC/crtls-stage/opt/devkitpro/devkitARM/." "$DEVKITARM/"
```

#### 4. Host tools and checks

Build `bin2s`, `3dsxtool`, `smdhtool` and the Picasso shader assembler for your host OS:

```sh
cd "$REGTA_SDK_SRC"
for tool in general-tools 3dstools picasso; do
  git clone "https://github.com/devkitPro/$tool.git" || break
  (
    set -e
    cd "$tool"
    ./autogen.sh
    ./configure --prefix="$DEVKITPRO/tools"
    make -j2
    make install
  ) || break
done
arm-none-eabi-gcc --version
arm-none-eabi-gcc -dumpfullversion
test -f "$DEVKITARM/3ds_rules"
test -f "$DEVKITARM/arm-none-eabi/lib/armv6k/fpu/3dsx_crt0.o"
command -v bin2s picasso 3dsxtool smdhtool
```

Expect **devkitARM release 55** and **10.2.0**. Keep the DEVKITPRO, DEVKITARM
and PATH exports in your shell startup file, or repeat them in each terminal.
The game compiles its bundled libctru, Citro3D, librw and audio libraries; do
not replace them with current prebuilt 3DS libraries.

#### 5. Clone and build the games

```sh
cd "$HOME"
git clone https://github.com/Epic0522/REGTA-3DSPort-Complete.git
cd REGTA-3DSPort-Complete
./scripts/verify-layout.sh
./scripts/build.sh all
```

The build checks the compiler version, not a private developer directory;
your SDK does not need to live inside this repository. Git preserves the shared
dependency symlinks. ZIP extraction or copying through a filesystem without
symlink support may break them, so run the layout check before compiling.

If bootstrap fails, report the OS version, CPU architecture, host compiler,
buildscripts commit and **first
actual error**, not just the last `make` line. A 403 during download and a C/C++
compiler error after extraction are different problems. Newer host compilers
and macOS SDKs may need additional compatibility work for these historical sources;
do not silently replace the r55 target toolchain with current GCC.

Official references: [r55 scripts and patches](https://github.com/devkitPro/buildscripts/tree/ef789eddb72421c1b3774767c02ee519cb38fc9e),
[rules 1.2.0](https://github.com/devkitPro/devkitarm-rules/tree/v1.2.0),
[startup objects 1.0.0](https://github.com/devkitPro/devkitarm-crtls/tree/v1.0.0),
[general-tools](https://github.com/devkitPro/general-tools),
[3dstools](https://github.com/devkitPro/3dstools),
[Picasso](https://github.com/devkitPro/picasso).

### Verify and compile (existing SDK)

Start from the repository root and verify the shared dependency links before
building:

```sh
./scripts/verify-layout.sh
```

Build one game or all three:

```sh
./scripts/build.sh re3
./scripts/build.sh revc
./scripts/build.sh relcs
./scripts/build.sh all
```

Expected outputs:

```text
III/build/re3.elf
III/build/re3.3dsx
miami/build/miami.elf
miami/build/miami.3dsx
stories/build/relcs.elf
stories/build/relcs.3dsx
```

Vice City builds as `miami.3dsx`; the install helper renames it to `revc.3dsx`.
The build helper sets the loading and lower-screen options used by the tested
builds, plus `OPTIMIZED_BUILD=1` for Vice City.

Do not reuse object files produced by another compiler or important flag set.
Clean only the affected tree and then rebuild it:

```sh
make -C III/build -f GNUmakefile clean       # GTA III
make -C miami/build -f GNUmakefile clean     # Vice City
make -C stories/build -f GNUmakefile clean   # LCS
```

Then rerun the matching `./scripts/build.sh` command. Test the result on your
console, especially after changing the compiler or build flags.

## Installing a 3DSX

Copy the build to your SD card with:

```sh
./scripts/install-3dsx.sh re3   "/Volumes/SD/3ds"
./scripts/install-3dsx.sh revc  "/Volumes/SD/3ds"
./scripts/install-3dsx.sh relcs "/Volumes/SD/3ds"
```

This produces `/3ds/re3.3dsx`, `/3ds/revc.3dsx` or `/3ds/relcs.3dsx`. The game
data remains in the directory shown in the architecture table above.

## CIA packaging

Once all three ELF files are built, you can package them as CIAs:

```sh
./packaging/production_cia/build_production.sh
```

The finished CGFX banners, encoded banner audio and 48×48 icons are included in
[packaging/prebuilt](packaging/prebuilt/README.md). Packaging uses them directly;
Blender, pycgfx, ImageMagick and the original vehicle assets are not needed.

Install zsh, bannertool, `makerom` and `3dsxtool` separately and put the
tools on `PATH`. You can also specify their executable paths with
`BANNERTOOL`, `MAKEROM` and `THREEDSXTOOL`. The generated banners,
icons and game packages are written only to the ignored output folder.

The completed packages are written to:

```text
packaging/production_cia/output/GTA3 For Nintendo 3DS.cia
packaging/production_cia/output/GTAVC For Nintendo 3DS.cia
packaging/production_cia/output/GTALCS For Nintendo 3DS.cia
```

| Game | Title ID | Product code | Long HOME title |
| --- | --- | --- | --- |
| GTA III | `00040000002F6000` | `CTR-P-0RE3` | Grand Theft Auto III |
| Vice City | `00040000002F6100` | `CTR-P-REVC` | Grand Theft Auto: Vice City |
| Liberty City Stories | `00040000002F6200` | `CTR-P-RLCS` | Grand Theft Auto: Liberty City Stories |

Production CIAs use New 3DS 804 MHz mode, L2 cache, expanded application
memory, direct SDMC access and the required video service. They contain no
commercial RomFS data and always load the original game files from the SD card.

CIA and 3DSX builds use the same SD data and saves.

### Build and setup troubleshooting

- `verify-layout.sh` reporting a missing dependency normally means symbolic
  links were flattened or one game tree was moved away from `common`. Restore
  the repository layout before compiling.
- `md5sum: command not found` occurs while makefiles calculate their build
  fingerprint. Install GNU coreutils; substituting macOS `md5` does not match
  the options used by the makefiles.
- A compiler-version or stale-fingerprint failure requires the affected
  makefile's `clean` target followed by `build.sh`; do not delete another game
  tree or the shared libraries.
- LCS setup errors about `.VB`, `sfx.RAW` or `gta_lcs.DAT` mean the selected
  directory is not the expected extracted PS2 data root. Select the directory
  that directly contains `AUDIO`, `DATA` and `models`.
- During Vice City radio conversion, FFmpeg may report `Error submitting packet
  to decoder: Invalid data found when processing input` once per original ADF.
  The stock streams contain a rejected packet, but conversion continues. If
  setup reaches `Prepared revc data` and the WAV files in `3ds/miami/Audio`
  play normally, no action is required.
- If CIA packaging cannot find bannertool, `makerom` or `3dsxtool`, check
  that the tool is installed and its path is set.
- A package that reaches HOME Menu but cannot find data usually has the wrong
  runtime directory or letter case. CIA builds still require `/3ds/re3`,
  `/3ds/miami` or `/3ds/relcs` exactly as documented.
- A very slow first launch with no native texture cache can be normal. Do not
  interrupt cache generation merely because ELF and CIA construction were
  quick.

## Shared Nintendo 3DS interface

All three games use Nintendo button labels and the same basic controls:

- Circle Pad controls movement or steering.
- C-stick controls the camera.
- ABXY, L/R and ZL/ZR are mapped to the games' native actions.
- START pauses and SELECT cycles the gameplay camera.
- The lower screen presents loading progress, radar/status information and
  contextual touch controls.
- Touch the lower screen once to reveal the overlay. The first touch only
  reveals it; release, then tap L3/R3 or drag the Camera region.
- The overlay hides after five seconds without touch input.
- Press L + R + ZL + ZR together during gameplay to open the 3DS system
  keyboard for text cheat entry.

Some actions differ between games; follow the in-game tutorials and button
prompts for those.

## Changelog

### Shared New Nintendo 3DS platform

#### Interface and controls

- Full lower-screen map, HUD, loading display and touch controls.
- Game-specific colours, fonts and icons.
- Fixed stale maps, loading flashes and HUD visibility during cutscenes.
- Nintendo button prompts throughout menus, shops and tutorials.
- A confirms; B returns, including all three games' shops.
- Adjusted Circle Pad and C-stick dead zones; fixed camera drift after transitions.
- R for third-person rifle aim; L + R for first-person aim.
- A/B for sniper zoom in/out.
- Clearer weapon sights at 3DS resolution.
- VC/LCS map: Y marker, ZR/R zoom, L legend, B back.
- L + R + ZL + ZR opens the text cheat keyboard.
- Full mission names in save lists, including supported older truncated titles.
- Removed desktop-only and ineffective settings from the 3DS menus.

#### Rendering and performance

- Fixed skinning, transparency, texture-state and geometry-rendering errors.
- Fixed foliage hiding scenery and transparent surfaces turning opaque.
- Fixed vehicle windows, lights, damage layers, decals and plates.
- Reduced excessive vehicle-highlight brightness.
- Faster animation loading and native texture streaming.
- Spread streaming work across frames to reduce stalls.
- Improved texture reclamation and low-memory handling.
- Reduced distant effects, collision debris, lighting and particle costs.
- Removed unused audio processing and redundant stereo work.
- Prioritised mission dialogue, weapons and other important sounds.
- Fixed duplicate sounds, audio-stream restarts and music-loop stutter.

#### Setup and presentation

- Shared build, data-preparation and installation scripts.
- devkitARM r55 / GCC 10.2 builds with toolchain checks.
- Separate CIA title IDs and SD data folders.
- New 3DS CPU, cache and expanded-memory support.
- Skippable hardware-decoded startup movies.
- Custom icons and animated HOME Menu vehicle banners.
- Smoothed banner logos; fixed the LCS logo edge and HOME Menu freeze.
- Final-mission music with looping, cutscene volume changes and ending fades.
- Radio switching disabled while final-mission music is active.
- Audio settings include **Final mission BGM**, ON by default. OFF stops the
  custom music and restores normal vehicle radio, station switching and station
  names. The setting is saved as `[Audio] FinalMissionBGM=0/1` in the game's INI.
  Re-enabling it mid-mission starts the current FM/LOOP section from its beginning.
  LCS mission fixes remain active with either setting.
- Classic controls are the default for fresh 3DS settings and restored controller
  defaults. Existing saved control choices are kept.

### Grand Theft Auto III / re3

- Complete dark-blue lower-screen interface and main-menu map.
- Faster animation loading and smoother texture streaming.
- Removed dynamic motion-blur trails.
- Reduced distant detail and costly effects in busy scenes.
- Matched LCS's per-model draw distances and fixed premature fading on large buildings.
- Fixed white vehicle polygons, overbright highlights and sunset colour errors.
- Restored transparent windows, lights and vehicle decals.
- Fixed the Staunton tower-clock crash.
- Fixed gameplay freezing while the radio kept playing.
- Added 'push it to the limit' to the final mission.
- Added the animated Kuruma HOME Menu banner.

### Grand Theft Auto: Vice City / reVC

- Complete pink lower-screen interface.
- Removed the multi-minute animation-loading delay.
- Faster collision loading and staged loading progress.
- Restored particle settings, colours and transparency.
- Fixed smoke, fire, rain and other effects appearing as opaque rectangles.
- Reduced automatic-weapon visual effects without changing damage.
- Fixed broken character polygons and mission-character rendering.
- Fixed vehicle colours, highlights, windows, decals and plates.
- Fixed textures staying blurry after memory pressure.
- Matched LCS's per-model draw distances and fixed premature fading on large buildings.
- Fixed boat rendering and missing water sections.
- Smoothed near/middle/far water transitions without removing detail levels.
- Fixed white foreground water.
- Fixed streaming crashes during flights.
- Improved mission and telephone dialogue loading.
- Added LCS-style mono ADPCM radio playback (run setup to convert the stations).
- Fixed answering phone calls with L in Standard controls.
- Restored normal pedestrian and traffic defaults.
- Fixed startup data-path handling and stale loading-screen textures.
- Added 'Self Control' to the final mission, with the climax after Lance's reveal.
- Added the animated Admiral HOME Menu banner.

### Grand Theft Auto: Liberty City Stories / reLCS

#### Campaign and saves

- Completed and repaired reStories so the main story can be finished on New 3DS.
- Added PS2 game-data support and 3DS audio conversion.
- Fixed mission scripts, introduction triggers and script loading.
- Restored saved world changes, buildings and collision.
- Fixed Callahan Bridge, lift-bridge, ferry and Fort Staunton states after loading.
- Fixed overlapping intact and destroyed Fort Staunton scenery.
- Preserved hidden-package rewards, safehouse pickups and special garage vehicles.
- Fixed saves with mission vehicles crashing on load.
- Fixed save/load ordering, script timers and shutdown crashes.
- Updated save prompts and restored the cheat warning.
- Added custom GTA-style cheat names, safehouse/mission teleports and checkpoint testing.

#### Missions and characters

- Fixed flamethrower objectives in 'Friggin’ the Riggin’'.
- Restored Portland's crusher magnet and crane models.
- Fixed leftover mission braking and radio restrictions.
- Fixed missing boats and occupants in 'The Sicilian Gambit'.
- Fixed the player's boat exploding at the lighthouse transition.
- Restored final-mission helicopter attacks and rocket damage.
- Fixed Massimo being knocked down while boarding the helicopter.
- Restored ending credits.
- Added 'Chase' to the final mission, including checkpoint restarts and the helicopter climax.
- Fixed broken high-detail cutscene characters and animation-memory errors.
- Fixed people disappearing after cutscenes.
- Fixed Toni standing through vehicles and freezing during arrest.
- Fixed looping landing animations and restored the landing roll.
- Fixed drive-by weapon selection after cutscenes.
- Restored race countdowns; removed broken checkpoint light columns.
- Kept mission titles visible for at least three seconds.
- Fixed mission-message fades and reward colours.
- Removed the oversized controller diagram to avoid pause-menu memory stalls.

#### World and vehicles

- Reduced large-building flicker and repaired distant-island LOD visibility.
- Restored streamed safehouse and mission interiors.
- Fixed foliage transparency and reduced its rendering cost.
- Fixed short-range water tiles and white near water.
- Fixed ferry colour changes and black polygons.
- Added lightweight vehicle reflections and smooth entry/exit transitions.
- Reduced reflection colour bleed and rainbow artefacts.
- Restored transparent vehicle windows, wheels, lights and reverse audio.
- Fixed overlapping decals and plates.
- Fixed glass-shattering crashes while keeping the original shard effect.
- Reduced excessive tyre smoke, dirt and other particle costs.
- Aligned default pedestrian and traffic density with the other ports.
- Added the animated Leone Sentinel HOME Menu banner.

LCS main-story completion is verified on hardware. Side missions, optional
activities and distant-island viewpoints still need more testing.

## Texture caches and first launch

The pause menu also caches its textures in 3DS-native format, beside the original
menu TXDs as `*.menu3ds-v1`. The first load creates these files; later loads reuse
them without converting the PC textures again. Closing the menu releases its
textures from memory. Controller diagrams are excluded; button prompts remain.
Replacing a source TXD (changed size or modification time) invalidates its cache.
Delete these generated files to force a rebuild. They are not release assets.

`models/txd.img` and `models/txd.dir` are generated native texture caches, not
original source assets. When they are absent, a build with `USE_TXD_CDIMAGE`
can create them from the installed game data. First-time conversion on the
console may take a long time. Preserve a known-good generated cache unless the
underlying textures or converter change.

The capability record (`DATA/CAPS.DAT` in LCS) is generated with the cache.
A fresh setup will not contain that record, the texture cache or personal
settings until the game creates them.

Use caches made by the matching converter, with mipmaps intact. An old or
incorrectly converted cache can still cause texture problems or slow streaming
even if its filenames are correct.

## Save data

Each game stores settings and saves under its own runtime directory:

```text
sdmc:/3ds/re3/userfiles/
sdmc:/3ds/miami/userfiles/
sdmc:/3ds/relcs/userfiles/
```

Back up `userfiles` before replacing save-conversion tools, testing modified
scripts, or migrating from an older runtime path. The setup helper preserves
an existing destination save directory, but a manual folder replacement may
not.

## Known limitations

- New 3DS-family hardware is required.
- Initial native texture conversion can be slow.
- Very busy scenes and demanding cutscenes can still cause frame drops or
  audio stutter.
- Environments without working MVD color conversion cannot start the renderer,
  including emulators that only stub out that service. This is a hardware
  dependency, not a guarantee against modified builds or future emulation.
- ASI plugins, CLEO scripts, binary desktop patches and desktop limit adjusters
  do not work. Their functionality must be integrated into source and rebuilt.
- reLCS required more reconstruction than re3 or reVC. The main story is
  playable to the end, but not every unused PSP/PS2 feature has been recreated.
- LCS main-story completion has been verified on a physical New Nintendo 3DS.
  Side missions and other optional activities have not yet been verified.

## Reporting bugs

If you run into a problem, please open an Issue in this repository. Include
as much of the following as you can:

- The game, build or commit, console model, and whether you use CIA or 3DSX.
- The mission name or location, steps to reproduce the problem, what you
  expected to happen, and what actually happened.
- Whether it happens every time, only after extended play, or after loading
  a save; mention any cheats or mods used.
- The crash dump (`crash_dump_*.dmp`) if one was generated, plus any relevant
  logs.
- Screenshots or a short video showing the problem. A save from just before
  the issue is helpful too.

You can still report a bug without a dump—for freezes, missing objects and
mission problems, clear reproduction steps and pictures are often more useful.

## Credits and legal notice

The LCS port is based on
[reStories by knackers4 and its contributors](https://github.com/knackers4/res).
We continued its unfinished reLCS implementation and adapted it for New 3DS.
The [PS2 asset converter](https://github.com/knackers4/res/releases/tag/relcs)
also comes from that project.

This project also builds on re3/reVC, the original community Nintendo 3DS port,
librw, devkitPro, libctru, Citro3D, OpenAL Soft, mpg123 and their contributors.

The source is provided for educational, documentation and modding purposes.
This project is not affiliated with Rockstar Games or Take-Two Interactive.
The full original games are not included. The selected overrides and HOME Menu
artwork do not replace the required game data. Please keep the upstream credits
and follow the licences of the code you use.
