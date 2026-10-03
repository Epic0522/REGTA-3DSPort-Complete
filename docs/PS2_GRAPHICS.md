# PS2 Graphics on Nintendo 3DS

Research date: October 3, 2026. GTA III, Vice City and Liberty City Stories are
investigated separately. San Andreas and Vice City Stories are context only;
neither supplies a numeric draw-distance contract for these three games.

## What the sources establish

- **III and VC:** Rockstar North's Aaron Garbut described a 2.5 km distant view
  when discussing Vice City, also referring to III. This is a skyline claim in
  a developer interview, not a guarantee that every tree, car or high-detail
  building renders at that distance. [Original graphics Q&A, September 27,
  2002](https://www.gamespot.com/articles/grand-theft-auto-vice-city-graphics-qanda/1100-2881042/).
- **III and VC effects:** SkyGFX's author identifies Trails as framebuffer
  post-processing. Switching it off also changes III's colour overlay and VC's
  ambient-light selection. Thus a soft screenshot alone cannot establish a
  shorter geometric range. [Author's guide](http://gta.rockstarvision.com/skygfx/skygfx.html).
- **LCS:** The Stories renderer is RSL, rather than the same low-level RenderWare
  renderer used by III and VC. A map-converter author reports that Stories
  placement matrices in LVZ/DTZ sectors limit rendering distance. That is an
  additional constraint beyond a weather far clip or an IDE model distance.
  [Converter author's investigation](https://gtaforums.com/topic/930285-lcsvcsrel-ps2-assets-for-psp/page/2/),
  [converter release](https://www.gtagarage.com/mods/show.php?id=25922).
- **LCS blur:** SkyGFX's Leeds effect reconstruction has different PS2 and PSP
  colour operations, offset current-frame copies and previous-frame blending.
  The 3DS implementation below approximates these operations with per-eye
  history and shifted current-frame overlays. [Author's implementation](https://github.com/aap/skygfx_vc/blob/master/src/postfx.cpp).

No reviewed source establishes a reproducible global PS2/PC percentage for III
or VC, or a measured LCS/VC ratio. LCS has no original retail PC version: PC
ports must be labelled as ports. Weather, time, camera, model categories and
PC draw-distance slider settings must match before measuring a ratio. The
observation that LCS looks shorter than VC is useful for testing, but remains
an observation, not a verified constant.

## Comparison images and their limits

The author-provided pairs below use matching locations to isolate the effect
of Trails and its lighting. They are **SkyGFX demonstrations on PC**, not
captures of a retail PS2 and PC pair. They demonstrate appearance, not console
range measurements. Click each thumbnail for the full-size image.

| Game | Trails on | Trails off |
| --- | --- | --- |
| III | [![III Trails on](http://gta.rockstarvision.com/skygfx/screenshots/iiivc/thumb/postfx_iii_blur.png)](http://gta.rockstarvision.com/skygfx/screenshots/iiivc/postfx_iii_blur.png) | [![III Trails off](http://gta.rockstarvision.com/skygfx/screenshots/iiivc/thumb/postfx_iii_noblur.png)](http://gta.rockstarvision.com/skygfx/screenshots/iiivc/postfx_iii_noblur.png) |
| VC | [![VC Trails on](http://gta.rockstarvision.com/skygfx/screenshots/iiivc/thumb/postfx_vc_blur.png)](http://gta.rockstarvision.com/skygfx/screenshots/iiivc/postfx_vc_blur.png) | [![VC Trails off](http://gta.rockstarvision.com/skygfx/screenshots/iiivc/thumb/postfx_vc_noblur.png)](http://gta.rockstarvision.com/skygfx/screenshots/iiivc/postfx_vc_noblur.png) |

The GTA Place preserves Rockstar's official PS2 screenshots for
[III](https://thegtaplace.com/gta3/ps2-screenshots/),
[VC](https://thegtaplace.com/vicecity/ps2-screenshots/) and
[LCS](https://thegtaplace.com/gtalcs/ps2-screenshots/).
The LCS Hepburn Heights screenshot has visible haze at the end of the road;
near vehicle and building textures remain readable. Promotional screenshots
are useful appearance references, but cannot provide controlled draw-distance
measurements.

[![LCS PS2, official Hepburn Heights screenshot](https://thegtaplace.com/images/gtalcs/screenshots/ps2/ps2_19.jpg)](https://thegtaplace.com/gtalcs/ps2-screenshots/)

[VC PS2/PC comparison by Vadim M](https://www.youtube.com/watch?v=E51Zn3QNRbo)
is an additional moving-image reference. Its author explicitly identifies
PCSX2 capture with increased resolution; this is not evidence of native PS2
image sharpness.

## Final 3DS profile

Finalized October 3, 2026 after physical-console feedback. This is a
PS2-inspired adaptation; the percentages below are chosen 3DS budgets, not
measured retail PS2/PC ratios. Shared parameters live in
`common/3ds/PS2GraphicsProfile.h`.

| Budget relative to the same non-PS2 weather/pressure profile | III | VC | LCS |
| --- | --- | --- | --- |
| Ordinary world range | 83% | 80% | 76% |
| Paired building, vehicle, pedestrian and vegetation high detail | 85% | 85% | 85% |
| Untextured silhouette transition within the fog band | 25–70% | 0–40% | 0–40% |

Single-model props retain their authored model distance. Random traffic
occupants fade over eight units at 70% of vehicle high-detail range, with a
12-unit minimum. Own/mission vehicles and boats, aircraft and trains retain
their existing occupant rules.

### Settings and atmosphere

`Graphics/PS2Graphics` is an independent boolean, enabled by default in all
three games. Explicit saved OFF/ON choices are respected; restoring display
defaults enables it. Flat/Stereo and Quality/Performance remain independent.
LCS's 3DS pedestrian/car density sliders are removed; underlying population
and profile budgets remain unchanged.

The front 55–65% of the ordinary range stays clear. Fog grows with camera-space
distance over the remaining band and caps at 217/255 opacity, preserving
silhouette contrast. Large building faces can span the clear and hazy regions.
Fog colour follows the current time/weather sky interpolation; post-grade
fog brightness is limited to 80% without replacing its hue with a fixed colour.
A separate shader bundle samples a 4 KiB alpha ramp after distance interpolation.
The normal shader bundle remains byte-identical to the committed baseline.
Fog parameters are initialized for every camera, including when disabled.

World colour follows each game's timecycle and desktop colourfilter formulas.
III uses its authored blur alpha; VC retains its fixed strength. LCS applies
35% of the added foreground tint and compensates fog separately to retain the
accepted distant blue haze. III/VC's recursive colour operations become a
channel multiplier, capped at four for one PICA stage. Extreme scripted colours
therefore remain an approximation. Ordinary screen washes are skipped while
this mode grades the frame.

Previous-world-frame mixing at 30/255 supplies trails; a two-pixel shifted
18% overlay supplies the faint edge echo. HUD is drawn afterwards. Each eye
has a separate native-format history: left RGBA8, 512 KiB; right RGB8, 384 KiB.
One raw tiled copy per eye retains the mixture before grading. Teleports,
camera cuts, gaps over 150 ms, Flat/Stereo transitions and depth/slider changes
invalidate history. Failed history allocation leaves range reduction and fog
available. No shared-eye buffer or framebuffer resampling is used.

### Distant geometry and skyline continuity

Opaque ordinary building atomics gradually blend to a neutral unlit material
when their entire bounding sphere enters the selected fog band. The completed
transition disables texture unit 0 and skips vertex lighting/UV work while
retaining geometry and distance fog. This is a draw-time saving: it does not
unload streamed TXDs. Texture, vertex or material alpha excludes the whole
atomic, as do vegetation proxies, entity fades, non-depth-writing and additive
passes. Glass, fences, trees, vehicles and pedestrians retain their normal
material path. Both eyes use the same decision.

LCS separates ordinary sector scanning/building admission from camera clipping.
The five existing whole-island LODs retain the authored weather far plane and
use the silhouette material when opaque. Ordinary buildings fade over the last
10% of their reduced budget using nearest-bound depth; roads are excluded.
Fog remains capped beyond that budget, so the island outline can stay visible
without extending detailed-city sector scans.

### Walking traffic

The original code does not simply prohibit traffic while walking: candidate
spawns can fail visibility, distance, collision, road and travel-direction checks,
while distant unseen cars occupy the ambient population budget.
`common/3ds/TrafficSpawn.h` gives three of four existing on-foot attempts to
roads behind the camera at 32 units, keeping one forward attempt. Actual screen
and collision checks remain mandatory. In the existing safe removal path,
unseen ordinary civilian cars beyond 60 units can be recycled while on foot.

Driving spawns, attempt counts and population/pool caps are unchanged. Visible,
police, special, parked, extended-range and timed set-piece cars retain their
protections, as do locked/interesting/deletable/crane/garage checks. Nearby
traffic still depends on valid road paths; the change adds no pool scans or
spawn attempts.

### GPU lifetime and HOME closing

Shutdown drains submitted GPU work before releasing textures and shaders.
It does not wait for another VBlank: HOME suspension disables those callbacks,
which made the earlier refresh wait hang on Closing. All three game loops drain
before unloading frontend textures. Existing LCS lightweight exit and libctru/HBL
return paths are preserved.

## Validation and release

The latest physical-console feedback accepted the version for finalization.
Reported visual/performance observations guided the per-game budgets; they are
not a controlled frame-rate benchmark for all four profiles.

The final code cleanup preserves these accepted parameters and behavior;
2,421,201 sampled profile outputs are bit-identical before and after cleanup.
Builds use devkitARM r55 / GCC 10.2.0. Host regression checks cover range/fog and colour
math, skyline admission/fades, material exclusions/state restoration, per-eye
history, settings persistence, walking spawn/retention safeguards and shutdown
queue lifetime. They complement physical-console feedback rather than replace it.

Packaging checks reverse-extract each CIA and verify content hashes, New 3DS
CPU/memory metadata, original HOME titles, icon/banner bytes and both embedded
shader bundles. `SHA256SUMS` covers all three CIA and three 3DSX artifacts.
The normal shader bundle's SHA-256 is
`c24f745be9ee13219394f95e9f58d7bca0e4beb5c8ee484225a8aa14a72e5942`.
Source layout and the curated installation manifest are checked separately;
this update changes no game assets or installation files.

Final artifacts: `release/ps2-graphics-final-20261003/`.
The accepted pre-cleanup package remains in
`release/ps2-graphics-skyline-traffic-20261003/` for comparison.
Work is on `codex/ps2-graphics`; finalization does not commit or push the branch.
