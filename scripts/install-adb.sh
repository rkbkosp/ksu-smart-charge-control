#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ADB=${ADB:-adb}
REMOTE_TMP=/data/local/tmp/charge-policy
REMOTE_MODULE=/data/adb/modules/charge-policy

"$ROOT/scripts/build-android.sh"

PIDS=$("$ADB" shell "su -c 'pidof charged'" 2>/dev/null | tr -d '\r')
if [ -n "$PIDS" ]; then
    "$ADB" shell "su -c 'kill -TERM $PIDS'"
    attempt=0
    while [ "$attempt" -lt 5 ] && "$ADB" shell "su -c 'pidof charged'" 2>/dev/null | grep -q '[0-9]'; do
        sleep 1
        attempt=$((attempt + 1))
    done
    if "$ADB" shell "su -c 'pidof charged'" 2>/dev/null | grep -q '[0-9]'; then
        echo "charged did not stop; refusing to replace a live installation" >&2
        exit 1
    fi
fi

"$ADB" shell "rm -rf '$REMOTE_TMP' && mkdir -p '$REMOTE_TMP'"
"$ADB" push "$ROOT/charge-policy/." "$REMOTE_TMP/" >/dev/null
"$ADB" shell "su -c 'rm -rf $REMOTE_MODULE; mkdir -p $REMOTE_MODULE; cp -a $REMOTE_TMP/. $REMOTE_MODULE/; chown -R 0:0 $REMOTE_MODULE; find $REMOTE_MODULE -type d -exec chmod 0755 {} \\;; find $REMOTE_MODULE -type f -exec chmod 0644 {} \\;; chmod 0755 $REMOTE_MODULE/boot-completed.sh $REMOTE_MODULE/uninstall.sh $REMOTE_MODULE/bin/charged'"
echo "Staged at $REMOTE_MODULE. Reboot, or run its boot-completed.sh explicitly for development."
