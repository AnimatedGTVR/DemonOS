#!/bin/sh
set -eu

# Butterscotch is an MPL-2.0 GameMaker runner. Keep the port reproducible:
# builds use one reviewed revision instead of whatever happens to be at the
# tip of the upstream default branch that day.
commit=9cb6287ec6b400b7b0ef8d4668e91ebea75761c3
output=${1:-build/butterscotch-upstream}
repository=https://github.com/AnimatedGTVR/Butterscotch.git

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
    echo "Butterscotch checkout is not the pinned commit" >&2
    exit 1
fi

printf '%s\n' "$commit" > "$output/.demonos-pinned"
echo "Butterscotch source pinned at $commit"
