#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MODULE_PROP=$ROOT/charge-policy/module.prop
TAG=${1:-}
VERSION=${TAG#v}

if ! printf '%s\n' "$VERSION" | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'; then
    echo "usage: $0 vMAJOR.MINOR.PATCH" >&2
    exit 2
fi

OLD_IFS=$IFS
IFS=.
set -- $VERSION
IFS=$OLD_IFS
MAJOR=$1
MINOR=$2
PATCH=$3

if [ "$MINOR" -gt 99 ] || [ "$PATCH" -gt 99 ]; then
    echo "minor and patch versions must be between 0 and 99" >&2
    exit 2
fi

VERSION_CODE=$((MAJOR * 10000 + MINOR * 100 + PATCH))
if [ "$VERSION_CODE" -le 0 ] || [ "$VERSION_CODE" -gt 2147483647 ]; then
    echo "calculated versionCode is outside the supported range" >&2
    exit 2
fi

TMP=$MODULE_PROP.tmp
trap 'rm -f "$TMP"' EXIT HUP INT TERM

awk -v version="$VERSION" -v code="$VERSION_CODE" '
    BEGIN { found_version = 0; found_code = 0 }
    /^version=/ { print "version=" version; found_version = 1; next }
    /^versionCode=/ { print "versionCode=" code; found_code = 1; next }
    { print }
    END { if (!found_version || !found_code) exit 1 }
' "$MODULE_PROP" >"$TMP"

mv "$TMP" "$MODULE_PROP"
trap - EXIT HUP INT TERM
printf 'version=%s\nversionCode=%s\n' "$VERSION" "$VERSION_CODE"
