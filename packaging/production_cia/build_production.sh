#!/bin/zsh
set -euo pipefail

script_dir="${0:A:h}"
workspace="${script_dir:h:h}"
banner_root="$workspace/packaging"
output="${OUTPUT_DIR:-$banner_root/production_cia/output}"
assets="$banner_root/prebuilt"
bannertool="${BANNERTOOL:-bannertool}"
makerom="${MAKEROM:-makerom}"
three_dsx_tool="${THREEDSXTOOL:-3dsxtool}"
rsf="$script_dir/container.rsf"

for required in "$bannertool" "$makerom" "$three_dsx_tool"; do
  if ! command -v "$required" >/dev/null 2>&1; then
    print -u2 "Missing required executable: $required"
    exit 1
  fi
done

for game in re3 revc relcs; do
  for suffix in .cgfx .bcwav -icon.png; do
    [[ -f "$assets/$game$suffix" ]] || {
      print -u2 "Missing packaging input: $assets/$game$suffix"
      exit 1
    }
  done
done
for elf in "$workspace/III/build/re3.elf" "$workspace/miami/build/miami.elf" "$workspace/stories/build/relcs.elf"; do
  [[ -f "$elf" ]] || { print -u2 "Build the game first: $elf"; exit 1; }
done

mkdir -p "$output"
# Use the finished artwork and encoded sound unchanged. No scene export or
# audio conversion is needed when packaging a new executable.
for game in re3 revc relcs; do
  "$bannertool" makebanner -ci "$assets/$game.cgfx" \
    -ca "$assets/$game.bcwav" -o "$output/$game.bnr"
done

"$bannertool" makesmdh \
  -s 'GTA3 For Nintendo 3DS' \
  -l 'Grand Theft Auto III' \
  -p 'Epic' \
  -i "$assets/re3-icon.png" \
  -f visible,extendedbanner \
  -r regionfree \
  -o "$output/gta3.smdh"
"$bannertool" makesmdh \
  -s 'GTAVC For Nintendo 3DS' \
  -l 'Grand Theft Auto: Vice City' \
  -p 'Epic' \
  -i "$assets/revc-icon.png" \
  -f visible,extendedbanner \
  -r regionfree \
  -o "$output/gtavc.smdh"
"$bannertool" makesmdh \
  -s 'GTALCS For Nintendo 3DS' \
  -l 'Grand Theft Auto: Liberty City Stories' \
  -p 'Epic' \
  -i "$assets/relcs-icon.png" \
  -f visible,extendedbanner \
  -r regionfree \
  -o "$output/gtalcs.smdh"

"$three_dsx_tool" \
  "$workspace/III/build/re3.elf" \
  "$output/GTA3 For Nintendo 3DS.3dsx" \
  --smdh="$output/gta3.smdh"
"$three_dsx_tool" \
  "$workspace/miami/build/miami.elf" \
  "$output/GTAVC For Nintendo 3DS.3dsx" \
  --smdh="$output/gtavc.smdh"
"$three_dsx_tool" \
  "$workspace/stories/build/relcs.elf" \
  "$output/GTALCS For Nintendo 3DS.3dsx" \
  --smdh="$output/gtalcs.smdh"

"$makerom" -f cia \
  -o "$output/GTA3 For Nintendo 3DS.cia" \
  -rsf "$rsf" -target t \
  -elf "$workspace/III/build/re3.elf" \
  -icon "$output/gta3.smdh" \
  -banner "$output/re3.bnr" \
  -DAPP_TITLE='GTA3 For' \
  -DAPP_PRODUCT_CODE='CTR-P-0RE3' \
  -DAPP_UNIQUE_ID=0x2F60

"$makerom" -f cia \
  -o "$output/GTAVC For Nintendo 3DS.cia" \
  -rsf "$rsf" -target t \
  -elf "$workspace/miami/build/miami.elf" \
  -icon "$output/gtavc.smdh" \
  -banner "$output/revc.bnr" \
  -DAPP_TITLE='GTAVC Fo' \
  -DAPP_PRODUCT_CODE='CTR-P-REVC' \
  -DAPP_UNIQUE_ID=0x2F61

"$makerom" -f cia \
  -o "$output/GTALCS For Nintendo 3DS.cia" \
  -rsf "$rsf" -target t \
  -elf "$workspace/stories/build/relcs.elf" \
  -icon "$output/gtalcs.smdh" \
  -banner "$output/relcs.bnr" \
  -DAPP_TITLE='GTALCS F' \
  -DAPP_PRODUCT_CODE='CTR-P-RLCS' \
  -DAPP_UNIQUE_ID=0x2F62

shasum -a 256 \
  "$output/GTA3 For Nintendo 3DS.3dsx" \
  "$output/GTAVC For Nintendo 3DS.3dsx" \
  "$output/GTALCS For Nintendo 3DS.3dsx" \
  "$output/GTA3 For Nintendo 3DS.cia" \
  "$output/GTAVC For Nintendo 3DS.cia" \
  "$output/GTALCS For Nintendo 3DS.cia"
