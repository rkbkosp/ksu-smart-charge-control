#!/system/bin/sh

MODDIR=${0%/*}

"$MODDIR/bin/charged" \
    --config "$MODDIR/config.conf" \
    >/dev/null 2>&1 &

exit 0
