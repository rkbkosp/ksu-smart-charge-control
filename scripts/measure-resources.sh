#!/bin/sh
set -eu

ADB=${ADB:-adb}
DURATION=${1:-1800}

case "$DURATION" in
    *[!0-9]*|'') echo "usage: $0 [duration_seconds]" >&2; exit 2 ;;
esac

PID=$("$ADB" shell "su -c 'pidof charged'" 2>/dev/null | tr -d '\r' | awk '{print $1}')
[ -n "$PID" ] || { echo "charged is not running" >&2; exit 1; }

snapshot() {
    LABEL=$1
    echo "=== $LABEL ==="
    "$ADB" shell "su -c 'printf \"pid=$PID wchan=\"; cat /proc/$PID/wchan; echo; grep -E \"^(VmRSS|VmHWM|Threads|voluntary_ctxt_switches|nonvoluntary_ctxt_switches):\" /proc/$PID/status; printf \"stat=\"; cat /proc/$PID/stat; printf \"fds=\"; ls /proc/$PID/fd | wc -l; printf \"tasks=\"; ls /proc/$PID/task | wc -l'"
    "$ADB" shell "su -c 'dumpsys meminfo $PID | grep -E \"TOTAL PSS|TOTAL RSS\"'" 2>/dev/null || true
}

snapshot start
echo "Sleeping $DURATION seconds on the host; this does not wake the daemon."
sleep "$DURATION"
snapshot end
