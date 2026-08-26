#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MODULE=$ROOT/charge-policy
OUTPUT=$ROOT/dist/charge-policy.zip

[ -x "$MODULE/bin/charged" ] || { echo "Build charge-policy/bin/charged first" >&2; exit 2; }
chmod 0755 "$MODULE/boot-completed.sh" "$MODULE/uninstall.sh" "$MODULE/bin/charged"
mkdir -p "$ROOT/dist"
rm -f "$OUTPUT"
(
    cd "$MODULE"
    COPYFILE_DISABLE=1 zip -X -q -r "$OUTPUT" .
)
echo "$OUTPUT"
