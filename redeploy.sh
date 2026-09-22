#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
hosttools="$script_dir/../hosttools/3dslink"

game=${1:-relcs}
ip=${2:-192.168.1.16}

case "$game" in
	re3|III|iii)
		name=re3
		built="$script_dir/III/build/re3.3dsx"
		;;
	revc|vc|miami)
		name=revc
		built="$script_dir/miami/build/miami.3dsx"
		;;
	relcs|lcs|stories)
		name=relcs
		built="$script_dir/stories/build/relcs.3dsx"
		;;
	*)
		echo "Unknown game: $game" >&2
		echo "Usage: ./redeploy.sh [re3|revc|relcs] [3ds-ip]" >&2
		exit 2
		;;
esac

[ -x "$hosttools" ] || {
	echo "3dslink not found at $hosttools" >&2
	echo "See the relcs-3ds-build skill for how to build it." >&2
	exit 3
}

[ -f "$built" ] || {
	echo "Build output is missing: $built" >&2
	echo "Run scripts/build.sh $name first." >&2
	exit 3
}

echo "Deploying $built to $ip ..." >&2
exec "$hosttools" -a "$ip" "$built"
