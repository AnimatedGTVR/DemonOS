#!/bin/sh
set -eu

# Fetches the freeware English-translated Cave Story data release
# (Studio Pixel's original "Doukutsu Monogatari", Aeon Genesis translation),
# mirrored by the long-running Cave Story Tribute fan site. The archive's
# own CaveStory/Readme.txt states "This program is freeware." -- unlike
# Wolf3D's shareware data, there is no license ambiguity here.

expected=aa87fa30bee9b4980640c7e104791354e0f1f6411ee0d45a70af70046aa0685f
output=${1:-build/nxengine-data}
archive="$output/cavestoryen.zip"
url="https://www.cavestory.one/downloads/cavestoryen.zip"

mkdir -p "$output"
if [ ! -f "$archive" ]; then
    temporary="$archive.part"
    rm -f "$temporary"
    curl -fL --retry 3 --output "$temporary" "$url"
    mv "$temporary" "$archive"
fi
actual=$(sha256sum "$archive" | awk '{print $1}')
if [ "$actual" != "$expected" ]; then
    echo "Cave Story data archive checksum mismatch" >&2
    echo "expected: $expected" >&2
    echo "actual:   $actual" >&2
    exit 1
fi

rm -rf "$output/CaveStory"
unzip -q "$archive" -d "$output" CaveStory/data/\* CaveStory/Readme.txt

if [ ! -f "$output/CaveStory/data/npc.tbl" ]; then
    echo "fetch-cavestory-data.sh: CaveStory/data/npc.tbl missing from archive" >&2
    exit 1
fi

printf '%s  %s\n' "$expected" "cavestoryen.zip" > "$output/SHA256SUMS"
echo "Cave Story freeware data verified in $output"
