#!/bin/bash
# Wrapper for qemu-pebble that adds -icount so emulated timers run off the
# instruction counter instead of host timers. Works around firmware timer-IRQ
# stalls seen under WSL2 (frozen clock/app timers after ~1-5 min).
# Use: PEBBLE_QEMU_PATH=<this script> pebble install --emulator emery
exec "$HOME/.pebble-sdk/SDKs/current/toolchain/bin/qemu-pebble" \
  -icount shift=auto,align=off,sleep=on "$@"
