#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
failed=0

for path in \
	common/librw common/libctru common/citro3d common/mpg123-ctr common/openal-soft-ctr \
	III/build/GNUmakefile miami/build/GNUmakefile stories/build/GNUmakefile \
	III/vendor/librw miami/vendor/librw stories/vendor/librw; do
	if [ ! -e "$project_root/$path" ]; then
		echo "Missing: $path" >&2
		failed=1
	fi
done

for game in III miami stories; do
	for dependency in librw libctru citro3d mpg123-ctr openal-soft-ctr; do
		link="$project_root/$game/vendor/$dependency"
		if [ ! -L "$link" ] || [ ! -e "$link" ]; then
			echo "Broken shared dependency: $game/vendor/$dependency" >&2
			failed=1
		fi
	done
done

[ "$failed" -eq 0 ] || exit 1
echo "REGTA source layout is complete; all shared dependency links resolve."

