#!/bin/sh
set -eu

# Fetches id Software's free Quake 1.06 shareware and extracts id1/pak0.pak.
#
# quake106.zip is the official floppy installer (install.bat + resource.1).
# resource.1 is an LHA self-extracting archive (the installer's DEICE step
# just runs that SFX), so bsdtar can open it directly.  The extracted
# game tree contains ID1/PAK0.PAK with progs.dat, maps/e1m1-e1m8.bsp,
# progs/*.mdl and the episode 1 sounds.

version=1.06
expected=ec6c9d34b1ae0252ac0066045b6611a7919c2a0d78a3a66d9387a8f597553239
output=${1:-build/quake-data}
archive="$output/quake106.zip"
url="https://idgames.cyberd.org/idstuff/quake/quake106.zip"

command -v bsdtar >/dev/null 2>&1 || {
    echo "fetch-quake.sh: bsdtar is required to open the LHA installer" >&2
    exit 1
}

mkdir -p "$output"
if [ ! -f "$archive" ]; then
    temporary="$archive.part"
    rm -f "$temporary"
    curl -fL --retry 3 --output "$temporary" "$url"
    mv "$temporary" "$archive"
fi
actual=$(sha256sum "$archive" | awk '{print $1}')
if [ "$actual" != "$expected" ]; then
    echo "Quake archive checksum mismatch" >&2
    echo "expected: $expected" >&2
    echo "actual:   $actual" >&2
    exit 1
fi

work="$output/work"
rm -rf "$work"
mkdir -p "$work"
# install.bat's DEICE step runs the LHA SFX that resource.1 actually is.
bsdtar -xf "$output/quake106.zip" -C "$work" resource.1
bsdtar -xf "$work/resource.1" -C "$work"

if [ ! -f "$work/ID1/PAK0.PAK" ]; then
    echo "fetch-quake.sh: ID1/PAK0.PAK missing from extracted installer" >&2
    exit 1
fi
install -m 0644 "$work/ID1/PAK0.PAK" "$output/pak0.pak"
for name in SLICNSE.TXT README.TXT TECHINFO.TXT; do
    if [ -f "$work/$name" ]; then
        install -m 0644 "$work/$name" "$output/$name"
    fi
done
rm -rf "$work"

printf '%s\n' "$version" > "$output/VERSION"
printf '%s  %s\n' "$expected" "quake106.zip" > "$output/SHA256SUMS"
echo "Quake $version shareware verified in $output"
