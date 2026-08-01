#!/bin/sh
set -eu

commit=dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284
output=${1:-build/doomgeneric-upstream}
repository=https://github.com/ozkl/doomgeneric.git

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
    echo "doomgeneric checkout is not the pinned commit" >&2
    exit 1
fi
# doomgeneric leaves I_Quit as a no-op when its host SDL path is disabled.
# A freestanding DemonOS build has a real exit() syscall wrapper, so make the
# engine's normal quit path terminate the userspace process without pulling in
# SDL. The checkout above makes this deterministic on every preparation run.
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if ! grep -q 'DemonOS supplies exit' "$output/doomgeneric/i_system.c"; then
    patch --batch --forward -d "$output" -p1 < \
        "$script_dir/../ports/doom/doomgeneric-demonos.patch"
fi
if ! grep -q 'DemonOS writable configuration' "$output/doomgeneric/m_config.c"; then
    patch --batch --forward -d "$output" -p1 < \
        "$script_dir/../ports/doom/doomgeneric-config-demonos.patch"
fi
printf '%s\n' "$commit" > "$output/.demonos-pinned"
echo "doomgeneric source pinned at $commit"
