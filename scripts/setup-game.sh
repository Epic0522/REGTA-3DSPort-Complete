#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

usage()
{
	cat <<'EOF'
Usage: scripts/setup-game.sh [re3|revc|relcs] [ORIGINAL_GAME_DIR] [SD_3DS_DIR]

With no arguments, the script asks for all three values interactively.
SD_3DS_DIR is the /3ds directory on the mounted SD card, not the card root.
The original game directory is read-only and is never modified.
EOF
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
	usage
	exit 0
fi

game=${1:-}
original=${2:-}
sd_3ds=${3:-}

if [ -z "$game" ]; then
	printf 'Game (re3 / revc / relcs): '
	IFS= read -r game
fi
if [ -z "$original" ]; then
	printf 'Original game directory: '
	IFS= read -r original
fi
if [ -z "$sd_3ds" ]; then
	printf 'Mounted SD card /3ds directory: '
	IFS= read -r sd_3ds
fi

case "$game" in
	re3|III|iii|3)
		game=re3
		source_tree="$project_root/III"
		;;
	revc|vc|miami)
		game=revc
		runtime_dir=miami
		source_tree="$project_root/miami"
		;;
	relcs|lcs|stories)
		game=relcs
		source_tree="$project_root/stories"
		;;
	*)
		echo "Unknown game: $game" >&2
		usage >&2
		exit 2
		;;
esac

runtime_dir=${runtime_dir:-$game}

[ -d "$original" ] || { echo "Original game directory does not exist: $original" >&2; exit 3; }
mkdir -p "$sd_3ds"
[ -d "$sd_3ds" ] || { echo "Cannot access SD /3ds directory: $sd_3ds" >&2; exit 4; }

original=$(CDPATH= cd -- "$original" && pwd)
sd_3ds=$(CDPATH= cd -- "$sd_3ds" && pwd)
target="$sd_3ds/$runtime_dir"

case "$target/" in
	"$original/"*)
		echo "Refusing to place the installation inside the original game directory." >&2
		exit 5
		;;
esac

case "$game" in
	re3)
		[ -f "$original/models/gta3.img" ] || [ -f "$original/MODELS/GTA3.IMG" ] || {
			echo "This does not look like a GTA III PC installation (models/gta3.img missing)." >&2
			exit 6
		}
		;;
	revc)
		[ -f "$original/models/gta3.img" ] || [ -f "$original/MODELS/GTA3.IMG" ] || {
			echo "This does not look like a GTA Vice City PC installation (models/gta3.img missing)." >&2
			exit 6
		}
		[ -f "$original/data/gta_vc.dat" ] || [ -f "$original/DATA/GTA_VC.DAT" ] || {
			echo "This does not look like GTA Vice City (data/gta_vc.dat missing)." >&2
			exit 6
		}
		command -v python3 >/dev/null 2>&1 || { echo "python3 is required to prepare VC continuous audio." >&2; exit 7; }
		command -v ffmpeg >/dev/null 2>&1 || { echo "ffmpeg is required to prepare VC continuous audio." >&2; exit 7; }
		;;
	relcs)
		[ -f "$original/DATA/gta_lcs.DAT" ] || [ -f "$original/data/gta_lcs.dat" ] || {
			echo "This does not look like an extracted LCS PS2 data directory (DATA/gta_lcs.DAT missing)." >&2
			exit 6
		}
		[ -f "$original/AUDIO/sfx.RAW" ] || {
			echo "LCS PS2 AUDIO/sfx.RAW is missing." >&2
			exit 6
		}
		for stream_dir in MUSIC NEWS CUTSCENE; do
			[ -d "$original/AUDIO/$stream_dir" ] || {
				echo "LCS PS2 AUDIO/$stream_dir directory is missing." >&2
				exit 6
			}
		done
		command -v c++ >/dev/null 2>&1 || { echo "c++ is required to build the LCS audio decoder." >&2; exit 7; }
		command -v ffmpeg >/dev/null 2>&1 || { echo "ffmpeg is required to convert LCS audio." >&2; exit 7; }
		;;
esac

command -v rsync >/dev/null 2>&1 || { echo "rsync is required." >&2; exit 7; }
mkdir -p "$target"

echo "Copying legally owned $game data to $target"
if [ "$game" = relcs ]; then
	# Copy the PS2 data set without its large source streams and split SFX banks.
	# The two conversion steps below create the exact formats used by the 3DS port.
	rsync -a \
		--exclude='.DS_Store' --exclude='._*' \
		--exclude='*.exe' --exclude='*.dll' \
		--exclude='AUDIO/MUSIC/*.VB' \
		--exclude='AUDIO/NEWS/*.VB' \
		--exclude='AUDIO/CUTSCENE/*.VB' \
		--exclude='AUDIO/SET*/' \
		--exclude='userfiles/' --exclude='userfiles_*/' \
		"$original/" "$target/"

	"$source_tree/tools/convert_lcs_music_adpcm_3ds.sh" "$original/AUDIO" "$target/AUDIO"
	"$source_tree/tools/convert_lcs_streams_3ds.sh" "$original/AUDIO" "$target/AUDIO"

	# Remove stale fallbacks left by an older installation.  Saves are outside
	# these paths and are deliberately preserved.
	find "$target/AUDIO/MUSIC" -maxdepth 1 -type f \( -iname '*.VB' -o -iname '*.MP3' \) -delete
	find "$target/AUDIO/NEWS" "$target/AUDIO/CUTSCENE" -maxdepth 1 -type f -iname '*.VB' -delete
	for set_dir in "$target/AUDIO"/SET0 "$target/AUDIO"/SET1 "$target/AUDIO"/SET2 "$target/AUDIO"/SET3 "$target/AUDIO"/SET4 "$target/AUDIO"/SET5 "$target/AUDIO"/SET6; do
		if [ -d "$set_dir" ]; then
			find "$set_dir" -depth -delete
		fi
	done
else
	# Keep the commercial PC binaries out of the SD data directory.  Game data,
	# audio, scripts, models, configuration and texture archives are retained.
	rsync -a \
		--exclude='.DS_Store' --exclude='._*' \
		--exclude='*.exe' --exclude='*.dll' \
		--exclude='*.dmp' --exclude='*.log' \
		--exclude='userfiles/' --exclude='userfiles_*/' \
		"$original/" "$target/"
fi

if [ "$game" = revc ]; then
	vc_audio=$(find "$original" -maxdepth 1 -type d -iname audio -print -quit)
	[ -n "$vc_audio" ] || { echo "Vice City Audio directory is missing." >&2; exit 7; }
	python3 "$source_tree/tools/convert_vc_radio_3ds.py" "$vc_audio" "$target/Audio" --overwrite
fi

copy_runtime_override()
{
	relative_path=$1
	source_path="$project_root/gamefiles/$game/$relative_path"
	target_path="$target/$relative_path"

	[ -e "$source_path" ] || {
		echo "Required runtime override is missing: $source_path" >&2
		exit 8
	}

	if [ -d "$source_path" ]; then
		mkdir -p "$target_path"
		rsync -a "$source_path/" "$target_path/"
	else
		mkdir -p "$(dirname -- "$target_path")"
		rsync -a "$source_path" "$target_path"
	fi
}

echo "Applying the hardware-tested REGTA 3DS runtime overrides"
case "$game" in
	re3)
		# Only the curated overrides below belong in a prepared installation.
		for relative_path in \
			TEXT/american.gxt \
			audio/music \
			data/PARTICLE.CFG \
			data/main_d.scm \
			data/main_freeroam.scm \
			movies
		do
			copy_runtime_override "$relative_path"
		done
		;;
	revc)
		# Keep the original Vice City generic vehicle materials.
		for relative_path in \
			Audio/music \
			TEXT/american.gxt \
			data/particle.cfg \
			movies
		do
			copy_runtime_override "$relative_path"
		done
		;;
	relcs)
		for relative_path in \
			AUDIO/MUSIC \
			movies \
			txd/LOADSC0.TXD
		do
			copy_runtime_override "$relative_path"
		done
		;;
esac

sync
echo "Prepared $game data: $target"
du -sh "$target"
echo "The original game directory was not changed."
