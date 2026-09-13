# RE3-3DSPort-Complete

RE3-3DSPort-Complete contains finished New Nintendo 3DS ports of **Grand Theft Auto III** (`re3`) and **Grand Theft Auto: Vice City** (`reVC`). They are based on the public 2021 re3/reVC source snapshots and the original community Nintendo 3DS port, then extensively repaired and completed for real hardware.

The goal is not to redesign either game. The ports preserve the original PC gameplay and content while making them stable, readable, responsive, and practical on New Nintendo 3DS hardware.

> [!IMPORTANT]
> You must provide files from legally owned PC copies of GTA III and/or GTA Vice City. Original models, maps, audio, scripts, and other commercial game data are not included, except for the small modified runtime overrides in each `gamefiles` directory that are required by this port.

## Supported hardware

- New Nintendo 3DS
- New Nintendo 3DS XL
- New Nintendo 2DS XL
- A homebrew-capable system for 3DSX, or CFW for CIA installation

Old Nintendo 3DS-family systems are not supported. The final CIA configuration enables the New 3DS 804 MHz CPU mode, L2 cache, 124 MiB application memory, Core 2, and the MVD video service.

## Repository layout

| Path | Purpose |
| --- | --- |
| `re3ctr/re3` | GTA III source, 3DS platform layer, embedded bottom-screen assets, and GTA III runtime overrides |
| `re3ctr/miami` | Vice City source, 3DS platform layer, embedded bottom-screen assets, and Vice City runtime overrides |
| `re3ctr/librw` | Shared RenderWare replacement and repaired 3DS renderer |
| `re3ctr/openal-soft-ctr` | Shared OpenAL backend for 3DS gameplay audio |
| `re3ctr/mpg123-ctr` | MP3 decoder used by the radio path |
| `re3ctr/libctru`, `re3ctr/citro3d` | Pinned platform libraries used by the legacy port |
| `toolchains/devkitARM-r55` | Required devkitARM release 55 / GCC 10.2 toolchain |
| `banner_work/production_cia` | Reproducible CIA packaging scripts, RSF, and verification notes |
| `re3ctr/release` | Historical release layout and texture converter; its old README describes the original WIP port and is retained only for reference |

The two game source directories are separate historical Git working trees. Their `vendor` entries are symbolic links to shared libraries under `re3ctr`, so keep the complete layout when cloning, archiving, or moving the project.

## Installation

The executable and legally owned PC data are installed separately. Both 3DSX and CIA builds read game data from fixed SD-card paths:

```text
sdmc:/3ds/re3/       GTA III
sdmc:/3ds/miami/     GTA Vice City
```

### GTA III

1. Download or clone this complete project.
2. Create `sdmc:/3ds/re3/` on the SD card.
3. Copy the contents of a clean PC installation of GTA III into that directory.
4. Copy everything from `re3ctr/re3/gamefiles/` over `sdmc:/3ds/re3/`, merging folders and replacing files when asked.
5. For 3DSX, copy `GTA3 For Nintendo 3DS.3dsx` (or `re3.3dsx` from a local build) to `sdmc:/3ds/` and launch it with the Homebrew Launcher.
6. For CIA, install `GTA3 For Nintendo 3DS.cia` with the normal CFW title installer and launch it from the HOME Menu. The game-data directory from steps 2–4 is still required.

### Vice City

1. Download or clone this complete project.
2. Create `sdmc:/3ds/miami/` on the SD card.
3. Copy the contents of a clean PC installation of GTA Vice City into that directory.
4. Copy everything from `re3ctr/miami/gamefiles/` over `sdmc:/3ds/miami/`, merging folders and replacing files when asked.
5. For 3DSX, copy `GTAVC For Nintendo 3DS.3dsx` (or `miami.3dsx` from a local build) to `sdmc:/3ds/` and launch it with the Homebrew Launcher.
6. For CIA, install `GTAVC For Nintendo 3DS.cia` and launch it from the HOME Menu. The game-data directory from steps 2–4 is still required.

Do not mix the two games' overrides. The historical `re3ctr/release/gamefiles` directory belongs to GTA III; the maintained per-game directories above are authoritative.

### Required runtime overrides

The overlay step is part of the installation contract, not an optional mod:

- GTA III `gamefiles/TEXT/american.gxt` contains readable Nintendo 3DS control and lower-screen radar instructions.
- Vice City `gamefiles/TEXT/american.gxt` is the exact runtime file used by the completed hardware build and contains the 3DS control wording.
- Vice City `gamefiles/data/particle.cfg` restores the complete 41-field Vice City particle definitions. The old reduced file breaks routing and produces opaque rectangles.
- Other files already present under `gamefiles` are upstream runtime overrides and optional controller/frontend resources.

Known final override hashes:

```text
GTA III gamefiles/TEXT/american.gxt
c4c898d9d2f3639073141d57c9e0845e2ceaf365b4b1954de7844d50d41cc0b3

Vice City gamefiles/TEXT/american.gxt
8c60513622d8933f7f561676a626a168dded9800a6bc249e4f134bb8dee0a422

Vice City gamefiles/data/particle.cfg
f1c54149bb4ee91339916b67eff9688544fdc562a09cc6a4c35570e2b3309e0d
```

### Texture conversion and first boot

The 3DS renderer uses native texture data and benefits greatly from `models/TXD.IMG` and `models/TXD.DIR`. If they are absent, the enabled `USE_TXD_CDIMAGE` path can create them from the copied PC data on first launch. This can take a very long time on the console. The historical `re3ctr/release/txdcnv` utility can perform conversion on a compatible desktop system before copying the game directory to the SD card.

Mipmaps are required for acceptable texture memory use and performance. Do not remove the generated TXD image after successful conversion.

### Optional startup movies

The port can play hardware-decoded New 3DS startup movies from each game's `movies` directory:

```text
movies/Logo.3mv
movies/Logo.pcm
movies/GTAtitles.3mv
movies/GTAtitles.pcm
```

`.3mv` is the port's MVD-compatible video container and `.pcm` is a 32 kHz mono sidecar. The final converted files are included under each game's `gamefiles/movies` directory and are installed by the normal overlay step. Playback remains optional: missing or unsupported files are skipped safely, and a main face/shoulder button skips playback.

## Controls and dual-screen interface

The same Nintendo-labelled control layout is used by both games. Physical ABXY labels match the rewritten in-game prompts.

### On foot

| Control | Action |
| --- | --- |
| Circle Pad | Move the character |
| C-stick | Move the camera |
| Lower-screen Camera region | Drag to move the camera |
| A | Sprint; zoom out while using a sniper sight |
| B | Jump; zoom in while using a sniper sight |
| Y | Enter a vehicle |
| X | Fire or use the equipped weapon |
| R | Aim, auto-target, assume the weapon's aiming pose, or enter its original scope |
| ZL / ZR | Cycle weapons; while locked on, change target where supported |
| R3 touch zone | Look behind |
| START | Pause the game |
| SELECT | Cycle the gameplay camera |

The behaviour of L on foot depends on the selected aiming mode:

| Aiming mode | L | R | Fire |
| --- | --- | --- | --- |
| `Classic` | Re-centre the camera behind the character | Aim, auto-target, or open the weapon sight | X |
| `Standard` | Fire towards the moving crosshair | Aim, open the original scope, or assume the aiming pose for weapons without a scope | L or X |

`Standard` therefore behaves like a mouse-style two-button layout: R prepares or aims the weapon and either L or X fires naturally towards the on-screen crosshair. Weapons with an original first-person sight, including the GTA III M16 and the corresponding Vice City rifles, retain that original sight and behaviour.

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
| ZL | Look left |
| ZR | Look right |
| ZL + ZR | Look behind |
| L3 touch zone | Sound the horn |
| C-stick / Camera touch region | Move the camera |
| START | Pause the game |
| SELECT | Cycle the vehicle camera |

The Standard L-to-fire alias is active only while walking. It is disabled in vehicles and menus, so L always keeps its normal driving function and changes the radio station in vehicles that have a radio.

### Lower screen and additional input

- Circle Pad radial dead zone: 0.14.
- C-stick radial dead zone: 0.18, with response scaling for its short physical travel.
- Touch the lower screen once to reveal the control overlay. The first touch only opens it, preventing accidental input.
- Release, then touch L3, R3, or drag in the Camera region to emulate missing stick buttons and right-stick camera input.
- The touch overlay hides after five seconds of inactivity.
- Press L + R + ZL + ZR together during gameplay to open the system keyboard for direct cheat-code entry through the original cheat parser.
- The D-pad is available for menu navigation and contextual or scripted game actions.
- A native 320×240 lower-screen camera renders a rectangular live map, mission blips, right-side status HUD, loading progress, and correct menu/pause/cutscene states.
- The upper-screen duplicate HUD is removed; wanted indicators remain visible when needed.

## Major features and repairs

### Shared 3DS platform work

- Completed bottom-screen map, HUD, loading, and touch integration for both games.
- Added correct cold-menu, gameplay, pause, short-streaming, loading, and cutscene lifecycle handling.
- Added Nintendo 3DS control labels and readable GXT tutorial text.
- Added optional MVD hardware-decoded startup movies.
- Added reproducible 3DSX/CIA packaging with custom icons and animated HOME Menu banners.
- Fixed rotated-framebuffer assumptions, stale buffers, loading-state transitions, out-of-bounds writes, and 3DS GPU-state leaks.
- Locked production to devkitARM release 55 / GCC 10.2. Newer builds may link successfully but crash on hardware because the port bundles legacy libctru/newlib-era code.

### Rendering and visual correctness

- Repaired 3DS RenderWare skin, MatFX, texture, alpha, immediate-mode, and raster-cache paths.
- Fixed black, white, or diamond-shaped polygons on characters and vehicles.
- Fixed corrupted vehicle bodies, transparent windows, lamps, reflections, and material colours.
- Fixed alpha state being lost after texture-cache hits.
- Added LRU texture reclamation and protected player/vehicle textures from destructive mip trimming, allowing detail to recover after memory pressure.
- Fixed opaque/rectangular particles by restoring definitions and combining colour textures with separate alpha masks.
- Fixed vehicle decals, badges, stripes, liveries, and licence plates affected by Z-fighting, including model-specific close surfaces.
- Fixed GTA III service-vehicle decals without biasing shared body, lamp, or trim atlases.
- Fixed the GTA III Staunton commercial-district tower-clock crash caused by double-indexed immediate-mode line vertices.
- Fixed Vice City boat/water corruption by drawing boats after the water surface. The accepted trade-off is that submerged hull geometry is hidden by the water-depth pass.
- Improved many Vice City vehicle detail layers; a few uncommon cars, including some stadium racers, may retain minor decal interference.
- Disabled GTA III's persistent temporal motion-blur trail for clearer output and lower cost.
- Set final 3DS LOD-distance scaling to 0.65 in both games.
- Replaced texture-sampled rocket-launcher sights in both games with solid four-corner geometry so filtering cannot fragment thin lines.
- Recreated the normal Standard-mode third-person sight as code-drawn geometry in both games. It preserves the original `siteM16` thin ring, centre dot, 0.4-scale proportions, and camera-derived screen position without sampling the fragile texture.
- Preserved the original sight rendering for scoped rifle modes (GTA III M16 and Vice City M4/Ruger/M60); the replacement applies only to the ordinary third-person Standard crosshair.
- Thickened GTA III's controller lock-on square with a 3×3 physical-pixel dilation while preserving its size and colour.
- Preserved Vice City's thicker dual-ring lock-on design.

### Streaming, loading, and memory

- Added 3DS-native file streaming and safer asynchronous read handling.
- Preserved GTA III's fast TXD CD-image path.
- Preloads standalone animation files to avoid thousands of tiny SD-card reads.
- Vice City reads `PED.IFP` once and parses it from memory, removing its multi-minute animation-loading bottleneck.
- Added hashed Vice City collision-model lookup instead of repeatedly scanning thousands of names.
- Added a two-channel Vice City loading pipeline, staged progress, animation preloading, and throttled lower-screen updates.
- Prevented one-frame gameplay streaming from flashing a loading screen.
- Added texture-pressure callbacks that release streamed resources and recover instead of failing permanently.
- Fixed helicopter traversal crashes over streaming-heavy Vice City routes.

### Audio

- Uses the real mono output contract instead of decoding and queuing an unused second channel.
- Retains the full 27-slot gameplay mix through the OpenAL path, with distance/source priority instead of globally reducing the voice count.
- Protects dialogue, mission audio, player actions, UI, weapons, explosions, and emergency sounds from low-priority ambient replacement.
- Suppresses duplicate one-shot requests and bounds repeated collision reports from the same pile-up.
- Uses non-blocking, bounded radio refills and handles `MPG123_NEW_FORMAT` without reopening a valid stream.
- Uses short radio startup buffering to avoid long vehicle-entry stalls.
- Prepares Vice City mission/cutscene dialogue outside the critical start frame and uses complete mono PCM buffers on dedicated NDSP channels where applicable.
- Avoids the former dual-source synchronization, short-block restart, and GPU-wait approaches that caused repetition, starvation, or freezes.

The final path greatly reduces ordinary sound pre-emption. Extremely heavy real-time cutscenes can still run below the rate needed to meet every audio deadline; this is a whole-system performance limit rather than a remaining channel-allocation bug.

### Performance budgets

- Caps live particles at 256 while retaining all particle types.
- Bounds secondary collision effects/audio, distant occupants, point lights, fire updates, explosion side effects, muzzle flashes, gun smoke, casings, and visible bullet traces.
- Keeps traffic simulation, wanted dispatch, weapon hits, explosions, fires, and core collision/physics intact.
- Removes unused or disproportionately expensive reflection and full-screen effects.
- Improves Vice City traffic/dispatch behaviour while preventing secondary rendering/audio work from overwhelming the ARM11.

### Vice City-specific repairs

- Replaced line-number-based particle loading with name-based routing for the full Vice City format.
- Restored smoke, fire, rain, dust, debris, blood, shell, heat-haze, and water-related alpha behaviour.
- Added safer per-frame skin buffers for protected mission characters.
- Fixed permanently degraded player textures after memory pressure.
- Fixed mission/telephone dialogue preparation stalls.
- Fixed several path, collision-store, ped-attractor, cutscene-object, water, and streaming edge cases found during complete-game testing.
- Corrected Admiral and other extremely close vehicle details with narrowly scoped geometry/depth treatment.

## Building from source

### Requirements

- A POSIX-like shell. The final build was produced on macOS; the makefiles originate from the Linux 3DS port.
- The complete repository layout, including shared `re3ctr` libraries and working symbolic links.
- devkitARM release 55 / GCC 10.2.0. `toolchains/devkitARM` must resolve to `devkitARM-r55`.
- GNU Make and normal devkitPro host tools.

Do not mix objects built by different devkitARM releases. If the toolchain or important compiler flags change, remove only the relevant game's generated `build/obj` directory and rebuild.

From the repository root:

```sh
export DEVKITPRO="$PWD/toolchains"
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"

make -C re3ctr/re3/build -f GNUmakefile -j6

make -C re3ctr/miami/build -f GNUmakefile -j6 \
  LOADING_PIPELINE=1 \
  OPTIMIZED_BUILD=1 \
  BOTTOM_LOADING=1 \
  BOTTOM_RADAR=1
```

Expected outputs:

```text
re3ctr/re3/build/re3.elf
re3ctr/re3/build/re3.3dsx
re3ctr/miami/build/miami.elf
re3ctr/miami/build/miami.3dsx
```

GTA III enables bottom loading, bottom radar, and TXD CD-image support by default. Production GTA III audio is intentionally mono; its makefile rejects `STEREO=1`. Vice City also ships as mono even though its historical makefile still contains a stereo target.

### CIA packaging

The packaging entry point is:

```sh
banner_work/production_cia/build_production.sh
```

It converts current ELFs into SMDH-bearing 3DSX files and CIAs, builds animated banners, and applies the final ExHeader/RSF contract. Final logo, icon, and banner-audio inputs are stored in `banner_work/production_cia/assets`. The script accepts `BLENDER`, `BANNERTOOL`, `MAKEROM`, `THREEDSXTOOL`, and `MAGICK` environment overrides for host-specific executable locations.

Final title metadata:

| Game | Title ID | Product code | HOME Menu title |
| --- | --- | --- | --- |
| GTA III | `00040000002F6000` | `CTR-P-0RE3` | GTA3 For Nintendo 3DS |
| Vice City | `00040000002F6100` | `CTR-P-REVC` | GTAVC For Nintendo 3DS |

Both final CIAs use `mvd:STD`, 804 MHz New 3DS mode, L2 cache, 124 MiB New 3DS memory, kernel release 2.44, direct SDMC access, and no RomFS. They still load game data from `sdmc:/3ds/re3` or `sdmc:/3ds/miami`.

The production CIAs were reverse-extracted after packaging. ExHeader/ExeFS hashes, banner/icon identity, New 3DS flags, MVD access, and final artifact copies were verified. Static package validation does not replace a final physical-console smoke test.

## Final verified package hashes

These hashes identify the archived final release files. `makerom` regenerates
unsigned outer CIA ticket/TMD metadata on each run, so a freshly rebuilt CIA
can have a different whole-file hash. For this final run, each CIA content hash
was checked against its TMD, and the extracted ExHeader, code, banner, and icon
hashes were independently recomputed. The extracted banner and icon also match
the packaging inputs byte-for-byte.

```text
GTA3 For Nintendo 3DS.3dsx
a75f18bdafa2a5dcaaaad932fc2066a2b065f36912f4485497867d8356a40bea

GTA3 For Nintendo 3DS.cia
ebe77a850b5d1878535d05f4fb1cf40c1201fdc103257f4fc125ae13e8e31eba

GTAVC For Nintendo 3DS.3dsx
0fd3911a3f11fc6fda9f2edb9aed26ac1e8f17301faeba49cdf8a92b3324e13b

GTAVC For Nintendo 3DS.cia
4dff6ae9021553627515538cb04dadfc121504793c0c8e3f8429013e41dacc60
```

The matching archived files are under
`banner_work/production_cia/artifacts/final-standard-l-fire-solid-sight-20260826/`.

## Known limitations

- New 3DS-family hardware only.
- First native texture conversion can be extremely slow on the console.
- Very heavy real-time cutscenes may still stutter and miss audio deadlines.
- A few uncommon Vice City vehicle detail layers are not completely free of Z-fighting.
- The boat fix intentionally lets the water-depth pass hide submerged hull geometry.
- Emulator/static checks do not substitute for physical-console confirmation.
- ASI, CLEO, binary code patches, and desktop limit adjusters do not work; code changes must be integrated and rebuilt.

## Credits and legal notice

This work depends on the original re3/reVC reverse-engineering project, the first Nintendo 3DS port, librw, devkitPro/libctru/citro3d, OpenAL Soft, mpg123, and their contributors.

The upstream project describes the source as intended for educational, documentation, and modding use. This repository does not encourage piracy or commercial use. Keep derivative source available and preserve relevant upstream notices. You are responsible for supplying legally owned game data and complying with the laws that apply to you.
