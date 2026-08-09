#!/bin/sh
set -eu

profile=${1:-}
source_path=${2:-}
destination=${3:-}

case "$profile" in
    deltarune-ch1|undertale-demo) ;;
    *) echo "unsupported Butterscotch game profile: $profile" >&2; exit 2 ;;
esac

if [ -d "$source_path" ]; then
    source_path=$(find "$source_path" -type f -iname data.win -print -quit)
fi
if [ -z "$source_path" ] || [ ! -f "$source_path" ]; then
    echo "data.win was not found in the supplied path" >&2
    exit 2
fi

magic=$(dd if="$source_path" bs=1 count=4 2>/dev/null)
if [ "$magic" != FORM ]; then
    echo "$source_path is not a GameMaker FORM package" >&2
    exit 3
fi

size=$(wc -c < "$source_path" | tr -d ' ')
if [ "$size" -lt 16 ]; then
    echo "$source_path is truncated ($size bytes)" >&2
    exit 3
fi

declared_hex=$(dd if="$source_path" bs=1 skip=4 count=4 2>/dev/null |
    od -An -tx1 | awk '{ print $4 $3 $2 $1 }')
declared=$((0x$declared_hex + 8))
if [ "$declared" -gt "$size" ]; then
    echo "$source_path declares $declared bytes but contains only $size" >&2
    exit 3
fi

mkdir -p "$(dirname "$destination")"
cp "$source_path" "$destination"
sha256sum "$destination" > "$destination.sha256"
echo "Imported $profile: $size bytes"
echo "Destination: $destination"
echo "SHA-256: $(cut -d ' ' -f 1 "$destination.sha256")"
echo "The source file remains the user's property and is not tracked by DemonOS."
