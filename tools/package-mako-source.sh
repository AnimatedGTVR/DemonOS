#!/bin/sh
set -eu

repo=$1
archive=$2
manifest=$3

origin=$(git -C "$repo" config --get remote.origin.url)
case "$origin" in
    https://github.com/AnimatedGTVR/MAKO.git|https://github.com/AnimatedGTVR/MAKO|git@github.com:AnimatedGTVR/MAKO.git)
        ;;
    *)
        echo "Refusing to package unexpected MAKO origin: $origin" >&2
        exit 1
        ;;
esac

commit=$(git -C "$repo" rev-parse HEAD)
short_commit=$(git -C "$repo" rev-parse --short HEAD)
dirty_files=$(git -C "$repo" status --porcelain=v1 | wc -l | tr -d ' ')
mkdir -p "$(dirname "$archive")" "$(dirname "$manifest")"

temporary_archive="$archive.tmp"
source_list="$archive.files"
uncompressed_archive="$archive.tar"
rm -f "$temporary_archive" "$source_list" "$uncompressed_archive"
git -C "$repo" ls-files --cached --others --exclude-standard -z > "$source_list"
tar -C "$repo" --no-recursion --null --files-from="$source_list" \
    --transform='s,^,MAKO/,' -cf "$uncompressed_archive"
zstd -19 -q "$uncompressed_archive" -o "$temporary_archive"
rm -f "$source_list" "$uncompressed_archive"
mv "$temporary_archive" "$archive"

archive_hash=$(sha256sum "$archive" | awk '{print $1}')
archive_bytes=$(wc -c < "$archive" | tr -d ' ')

{
    printf 'MAKO repository installation\n'
    printf 'origin=%s\n' "$origin"
    printf 'commit=%s\n' "$commit"
    printf 'short_commit=%s\n' "$short_commit"
    printf 'working_tree_changes=%s\n' "$dirty_files"
    printf 'archive=/system/mako/MAKO-source.tar.zst\n'
    printf 'archive_format=tar+zstd\n'
    printf 'archive_bytes=%s\n' "$archive_bytes"
    printf 'archive_sha256=%s\n' "$archive_hash"
    printf 'contents=tracked and non-ignored working-tree source files\n'
    printf 'runtime_status=source preinstalled; in-system compiler execution requires VFS and runtime support\n'
} > "$manifest"
