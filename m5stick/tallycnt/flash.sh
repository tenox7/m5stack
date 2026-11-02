#!/bin/bash
set -e
PORT=$(ls /dev/cu.usbserial-* 2>/dev/null | head -1)
[ -z "$PORT" ] && { echo "No USB serial device found"; exit 1; }
arduino-cli compile --fqbn m5stack:esp32:m5stack_stickc_plus2 --upload --port "$PORT" "$(dirname "$0")"
