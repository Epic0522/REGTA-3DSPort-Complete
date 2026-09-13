#!/bin/sh
set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
	echo "usage: $0 SOURCE_AUDIO_DIR DEST_AUDIO_DIR [sample-rate]" >&2
	exit 2
fi

source_audio=$1
dest_audio=$2
sample_rate=${3:-24000}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/relcs-music-adpcm.XXXXXX")
decoder="$work_dir/lcs_vb_decode"
cleanup()
{
	[ ! -d "$work_dir" ] || find "$work_dir" -depth -delete
}
trap cleanup EXIT HUP INT TERM

c++ -O3 -std=c++11 "$script_dir/lcs_vb_decode.cpp" -o "$decoder"

find "$source_audio/MUSIC" -type f -iname '*.VB' -print0 |
while IFS= read -r -d '' input; do
	relative=${input#"$source_audio"/}
	output="$dest_audio/${relative%.*}.WAV"
	mkdir -p "$(dirname -- "$output")"
	echo "3DS mono IMA ADPCM: $relative"
	"$decoder" "$input" |
		ffmpeg -hide_banner -loglevel error -y \
			-f s16le -ar 32000 -ac 1 -i pipe:0 \
			-ar "$sample_rate" -ac 1 -c:a adpcm_ima_wav \
			-map_metadata -1 "$output"
done
