#!/bin/sh
set -eu

commit=16bf776febef2a041cf07677f663c4cca2e810a1
output=${1:-build/nxengine-upstream}
repository=https://github.com/EXL/NXEngine.git

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
    echo "NXEngine checkout is not the pinned commit" >&2
    exit 1
fi

printf '%s\n' "$commit" > "$output/.demonos-pinned"
echo "NXEngine source pinned at $commit"
