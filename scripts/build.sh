#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
game=${1:-all}

: "${DEVKITPRO:=/opt/devkitpro}"
: "${DEVKITARM:=$DEVKITPRO/devkitARM}"
export DEVKITPRO DEVKITARM
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"

[ -x "$DEVKITARM/bin/arm-none-eabi-g++" ] || {
	echo "devkitARM r55 was not found at: $DEVKITARM" >&2
	echo "Download it separately, then set DEVKITPRO and DEVKITARM before building." >&2
	exit 2
}

if command -v sysctl >/dev/null 2>&1; then
	jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
else
	jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
fi

build_one()
{
	case "$1" in
		re3)
			make -C "$project_root/III/build" -f GNUmakefile -j"$jobs"
			;;
		revc)
			make -C "$project_root/miami/build" -f GNUmakefile -j"$jobs" \
				LOADING_PIPELINE=1 BOTTOM_LOADING=1 BOTTOM_RADAR=1 OPTIMIZED_BUILD=1
			;;
		relcs)
			make -C "$project_root/stories/build" -f GNUmakefile -j"$jobs" \
				LOADING_PIPELINE=1 BOTTOM_LOADING=1 BOTTOM_RADAR=1
			;;
		*)
			echo "Usage: scripts/build.sh [all|re3|revc|relcs]" >&2
			exit 3
			;;
	esac
}

case "$game" in
	all)
		build_one re3
		build_one revc
		build_one relcs
		;;
	re3|III|iii) build_one re3 ;;
	revc|vc|miami) build_one revc ;;
	relcs|lcs|stories) build_one relcs ;;
	*) build_one "$game" ;;
esac
