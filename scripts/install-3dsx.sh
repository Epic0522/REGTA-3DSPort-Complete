#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

if [ "$#" -ne 2 ]; then
	echo "Usage: scripts/install-3dsx.sh [re3|revc|relcs] SD_3DS_DIR" >&2
	exit 2
fi

case "$1" in
	re3|III|iii)
		name=re3
		built="$project_root/III/build/re3.3dsx"
		;;
	revc|vc|miami)
		name=revc
		built="$project_root/miami/build/miami.3dsx"
		;;
	relcs|lcs|stories)
		name=relcs
		built="$project_root/stories/build/relcs.3dsx"
		;;
	*)
		echo "Unknown game: $1" >&2
		exit 2
		;;
esac

[ -f "$built" ] || {
	echo "Build output is missing: $built" >&2
	echo "Run scripts/build.sh $name first." >&2
	exit 3
}

sd_3ds=$2
mkdir -p "$sd_3ds"
sd_3ds=$(CDPATH= cd -- "$sd_3ds" && pwd)
cp -p "$built" "$sd_3ds/$name.3dsx"
sync
echo "Installed $sd_3ds/$name.3dsx"

