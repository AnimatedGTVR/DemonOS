#!/bin/sh
set -eu

commit=5387b99d32fc5bac39c87defcb0abbf1018d8083
output=${1:-build/wolf4sdl-upstream}
repository=https://github.com/mozzwald/wolf4sdl.git

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
    echo "wolf4sdl checkout is not the pinned commit" >&2
    exit 1
fi

printf '%s\n' "$commit" > "$output/.demonos-pinned"
echo "Wolf4SDL source pinned at $commit"
