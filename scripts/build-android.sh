#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ -n "${ANDROID_NDK_HOME:-}" ]; then
    NDK=$ANDROID_NDK_HOME
elif [ -n "${ANDROID_NDK_ROOT:-}" ]; then
    NDK=$ANDROID_NDK_ROOT
else
    SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
    if [ -z "$SDK" ] && command -v adb >/dev/null 2>&1; then
        SDK=$(CDPATH= cd -- "$(dirname -- "$(command -v adb)")/.." && pwd)
    fi
    if [ -z "$SDK" ] && [ -n "${HOME:-}" ]; then
        if [ -d "$HOME/Library/Android/sdk" ]; then
            SDK=$HOME/Library/Android/sdk
        elif [ -d "$HOME/Android/Sdk" ]; then
            SDK=$HOME/Android/Sdk
        fi
    fi
    [ -n "$SDK" ] || { echo "Set ANDROID_NDK_HOME or ANDROID_SDK_ROOT" >&2; exit 2; }
    NDK=$(find "$SDK/ndk" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort | tail -n 1)
fi

[ -n "${NDK:-}" ] && [ -d "$NDK" ] || { echo "Android NDK not found" >&2; exit 2; }
exec make -C "$ROOT" NDK="$NDK" android
