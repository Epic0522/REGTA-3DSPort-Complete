#!/bin/sh
set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
	echo "usage: $0 SOURCE_AUDIO_DIR DEST_AUDIO_DIR [bitrate]" >&2
	exit 2
fi

source_audio=$1
dest_audio=$2
bitrate=${3:-64k}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/relcs-audio.XXXXXX")
decoder="$work_dir/lcs_vb_decode"
cleanup()
{
	[ ! -d "$work_dir" ] || find "$work_dir" -depth -delete
}
trap cleanup EXIT HUP INT TERM

c++ -O3 -std=c++11 "$script_dir/lcs_vb_decode.cpp" -o "$decoder"

# Radio/music uses the cheaper IMA ADPCM WAV route on 3DS.  MP3 is retained
# only for NEWS and CUTSCENE streams, where no WAV counterpart is generated.
find "$source_audio/NEWS" "$source_audio/CUTSCENE" \
	-type f -iname '*.VB' -print0 |
while IFS= read -r -d '' input; do
	relative=${input#"$source_audio"/}
	output="$dest_audio/${relative%.*}.MP3"
	mkdir -p "$(dirname -- "$output")"
	echo "3DS mono audio: $relative"
	"$decoder" "$input" |
		ffmpeg -hide_banner -loglevel error -y \
			-f s16le -ar 32000 -ac 1 -i pipe:0 \
			-ar 24000 -ac 1 -c:a libmp3lame -b:a "$bitrate" \
			-map_metadata -1 "$output"
done
