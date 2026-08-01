#!/bin/sh
set -eu

version=0.13.0
expected=3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59
output=${1:-build/freedoom}
archive="$output/freedoom-$version.zip"
base="freedoom-$version"
url="https://github.com/freedoom/freedoom/releases/download/v$version/freedoom-$version.zip"

mkdir -p "$output"
if [ ! -f "$archive" ]; then
    temporary="$archive.part"
    rm -f "$temporary"
    curl -fL --retry 3 --output "$temporary" "$url"
    mv "$temporary" "$archive"
fi
actual=$(sha256sum "$archive" | awk '{print $1}')
if [ "$actual" != "$expected" ]; then
    echo "Freedoom archive checksum mismatch" >&2
    echo "expected: $expected" >&2
    echo "actual:   $actual" >&2
    exit 1
fi

for name in freedoom1.wad freedoom2.wad COPYING.txt; do
    count=$(unzip -Z1 "$archive" | awk -v path="$base/$name" '$0 == path { n++ } END { print n+0 }')
    if [ "$count" -ne 1 ]; then
        echo "Expected exactly one $base/$name in release archive" >&2
        exit 1
    fi
    temporary="$output/$name.part"
    unzip -p "$archive" "$base/$name" > "$temporary"
    mv "$temporary" "$output/$name"
done
printf '%s\n' "$version" > "$output/VERSION"
printf '%s  %s\n' "$expected" "freedoom-$version.zip" > "$output/SHA256SUMS"
echo "Freedoom $version assets verified in $output"
