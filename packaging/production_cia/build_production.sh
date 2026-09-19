#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
workspace="$(cd "$script_dir/../.." && pwd)"
banner_root="$workspace/packaging"
output="${OUTPUT_DIR:-$banner_root/production_cia/output}"
assets="$banner_root/prebuilt"
bannertool="${BANNERTOOL:-bannertool}"
makerom="${MAKEROM:-makerom}"
three_dsx_tool="${THREEDSXTOOL:-3dsxtool}"
rsf="$script_dir/container.rsf"

# Per-game record: elf relative path|smdh basename|short name|long name|
# APP_TITLE (must be exactly 8 chars)|product code|unique id
declare -A GAME_RECORD=(
  [re3]="III/build/re3.elf|gta3|GTA3 For Nintendo 3DS|Grand Theft Auto III|GTA3 For|CTR-P-0RE3|0x2F60"
  [revc]="miami/build/miami.elf|gtavc|GTAVC For Nintendo 3DS|Grand Theft Auto: Vice City|GTAVC Fo|CTR-P-REVC|0x2F61"
  [relcs]="stories/build/relcs.elf|gtalcs|GTALCS For Nintendo 3DS|Grand Theft Auto: Liberty City Stories|GTALCS F|CTR-P-RLCS|0x2F62"
)
ALL_GAMES=(re3 revc relcs)

# Accept an optional list of games to package (e.g. `build_production.sh
# relcs`). Defaults to all three, matching the original behaviour.
if [[ $# -gt 0 ]]; then
  games=("$@")
else
  games=("${ALL_GAMES[@]}")
fi

for game in "${games[@]}"; do
  if [[ -z "${GAME_RECORD[$game]:-}" ]]; then
    echo "Unknown game: $game (expected one of: ${ALL_GAMES[*]})" >&2
    exit 1
  fi
done

for required in "$bannertool" "$makerom" "$three_dsx_tool"; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "Missing required executable: $required" >&2
    exit 1
  fi
done

for game in "${games[@]}"; do
  for suffix in .cgfx .bcwav -icon.png; do
    [[ -f "$assets/$game$suffix" ]] || {
      echo "Missing packaging input: $assets/$game$suffix" >&2
      exit 1
    }
  done
done
for game in "${games[@]}"; do
  IFS='|' read -r elf_rel _ <<<"${GAME_RECORD[$game]}"
  elf="$workspace/$elf_rel"
  [[ -f "$elf" ]] || { echo "Build the game first: $elf" >&2; exit 1; }
done

mkdir -p "$output"
# Use the finished artwork and encoded sound unchanged. No scene export or
# audio conversion is needed when packaging a new executable.
for game in "${games[@]}"; do
  "$bannertool" makebanner -ci "$assets/$game.cgfx" \
    -ca "$assets/$game.bcwav" -o "$output/$game.bnr"
done

artifacts=()
for game in "${games[@]}"; do
  IFS='|' read -r elf_rel smdh short_name long_name app_title product_code unique_id <<<"${GAME_RECORD[$game]}"
  elf="$workspace/$elf_rel"

  "$bannertool" makesmdh \
    -s "$short_name" \
    -l "$long_name" \
    -p 'Epic' \
    -i "$assets/$game-icon.png" \
    -f visible,extendedbanner \
    -r regionfree \
    -o "$output/$smdh.smdh"

  "$three_dsx_tool" \
    "$elf" \
    "$output/$short_name.3dsx" \
    --smdh="$output/$smdh.smdh"

  "$makerom" -f cia \
    -o "$output/$short_name.cia" \
    -rsf "$rsf" -target t \
    -elf "$elf" \
    -icon "$output/$smdh.smdh" \
    -banner "$output/$game.bnr" \
    -DAPP_TITLE="$app_title" \
    -DAPP_PRODUCT_CODE="$product_code" \
    -DAPP_UNIQUE_ID="$unique_id"

  artifacts+=("$output/$short_name.3dsx" "$output/$short_name.cia")
done

if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "${artifacts[@]}"
else
  shasum -a 256 "${artifacts[@]}"
fi
