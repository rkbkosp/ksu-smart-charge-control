# PJE110 OPlus charging backend

This file is the authority for device-specific writes made by `charged`.
The target is PJE110 / kalama / crDroid under KernelSU root.

## Inputs

The backend reads only:

```text
/sys/class/power_supply/usb/online
/sys/class/power_supply/battery/capacity
/proc/oplus-votable/CHG_DISABLE/force_active
/proc/oplus-votable/CHG_DISABLE/force_val
```

`usb/online` is the external-power state on this target. Battery `capacity`
is an integer percentage.

## Pause and resume

The numerical policy limit is enforced with the OPlus `CHG_DISABLE` debug
force vote. At a limit below 100, charging is paused when
`capacity >= desired_limit` and resumed when capacity drops below the limit.
For integer SOC this gives a one-percentage-point resume threshold (for
example, pause at 79 and resume at 78). A desired limit of 100 always clears
the debug force vote and leaves full-charge behavior to the kernel.

Before changing either force file, the daemon applies mode `0666` to both.
The required write order is:

```text
pause:  force_val=1, then force_active=1
resume: force_active=0, then force_val=0
```

The daemon first reads both values and performs no chmod or write if they
already represent the requested state. The vote is sticky across charger
replug, so no keepalive loop is used. On graceful shutdown and uninstall,
the vote is cleared to fail open.

The resulting pause keeps USB powering the device while battery current falls
near zero and battery status becomes `Not charging`. The optional
`/dev/bypass` marker is not a charging control and is never touched.

## Explicitly unsupported nodes

The daemon must not probe or write fallback controls. In particular it does
not touch `WIRED_CHARGING_DISABLE`, `CHG_SUSPEND`, qcom `restrict_chg`, generic
`input_suspend`/`charging_enabled`/`night_charging`, or
`/sys/devices/virtual/oplus_chg/battery/mmi_charging_enable`.
