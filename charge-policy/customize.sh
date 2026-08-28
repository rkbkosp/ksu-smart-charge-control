#!/system/bin/sh

# KernelSU applies default permissions after extracting module files.
# The native daemon must remain executable when boot-completed.sh starts it.
set_perm "$MODPATH/bin/charged" 0 0 0755
