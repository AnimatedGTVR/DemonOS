#!/bin/sh
set -eu

# Pinned, reproducible ClassiCube source acquisition. This is deliberately an
# explicit target: normal kernel/ISO builds never require network access.
commit=9a3101c00330aa6ca0e091bcd5c76d019ee85b7e
output=${1:-build/classicube-upstream}
repository=https://github.com/ClassiCube/ClassiCube.git

mkdir -p "$(dirname -- "$output")"

if [ ! -d "$output/.git" ]; then
    if [ -e "$output" ]; then
        echo "$output exists but is not a Git checkout" >&2
        exit 1
    fi
    git clone --filter=blob:none --no-checkout "$repository" "$output"
fi
git -C "$output" fetch --depth 1 origin "$commit"
git -C "$output" checkout --detach "$commit"
actual=$(git -C "$output" rev-parse HEAD)
if [ "$actual" != "$commit" ]; then
    echo "ClassiCube checkout is not the pinned commit" >&2
    exit 1
fi
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if ! grep -q 'defined __demonos__' "$output/src/Core.h"; then
    git -C "$output" apply --ignore-space-change \
        "$script_dir/../ports/classicube/classicube-demonos-core.patch"
fi
printf '%s\n' "$commit" > "$output/.demonos-pinned"
echo "ClassiCube source pinned at $commit"
