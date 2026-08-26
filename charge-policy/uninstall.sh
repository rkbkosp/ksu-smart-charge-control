#!/system/bin/sh

# Ask the daemon to exit; its SIGTERM path restores charging.
for PID in $(pidof charged 2>/dev/null); do
    kill -TERM "$PID" 2>/dev/null
done

# Also fail open if the daemon was not running or did not handle the signal.
ACTIVE=/proc/oplus-votable/CHG_DISABLE/force_active
VALUE=/proc/oplus-votable/CHG_DISABLE/force_val
chmod 666 "$ACTIVE" "$VALUE" 2>/dev/null
echo 0 >"$ACTIVE" 2>/dev/null
echo 0 >"$VALUE" 2>/dev/null

exit 0
