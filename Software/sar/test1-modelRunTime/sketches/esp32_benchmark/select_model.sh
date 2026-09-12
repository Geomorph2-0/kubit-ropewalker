#!/usr/bin/env bash
# select_model.sh <backbone> <size>
#   backbone : custom | mobilenetv3
#   size     : 64 | 96 | 128 | 160 | 320x240 | 480x320
#
# Rewrites the 5 "EDIT PER MODEL" #defines in esp32_benchmark.ino in one shot
# (header path, array symbol, MODEL_W, MODEL_H, BACKBONE) so they can never drift.
set -euo pipefail

bb=${1:-}; sz=${2:-}
case "$bb" in custom|mobilenetv3) ;; *) echo "backbone must be 'custom' or 'mobilenetv3'"; exit 1;; esac
case "$sz" in
  64|96|128|160) w=$sz; h=$sz; sym="model_${sz}_int8" ;;
  320x240)       w=320; h=240; sym="model_320_240_int8" ;;
  480x320)       w=480; h=320; sym="model_480_320_int8" ;;
  *) echo "size must be one of: 64 96 128 160 320x240 480x320"; exit 1 ;;
esac

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
hdr="$root/models/${bb}_${sz}/model_${sz}_int8.h"
[ -f "$hdr" ] || { echo "not found: $hdr"; exit 1; }
grep -q "^const unsigned char ${sym}\[\]" "$hdr" || { echo "symbol '$sym' not in $hdr"; exit 1; }

ino="$here/esp32_benchmark.ino"
sed -i \
  -e "s|^#define BENCH_MODEL_HDR .*|#define BENCH_MODEL_HDR  \"$hdr\"|" \
  -e "s|^#define BENCH_MODEL_SYM .*|#define BENCH_MODEL_SYM  $sym   // array name INSIDE that header ('_' not 'x')|" \
  -e "s|^#define MODEL_W .*|#define MODEL_W          $w|" \
  -e "s|^#define MODEL_H .*|#define MODEL_H          $h|" \
  -e "s|^#define BACKBONE .*|#define BACKBONE         \"${bb}_${sz}\"|" \
  "$ino"

echo "selected: ${bb}_${sz}  (sym=$sym  ${w}x${h})"
grep -nE '^#define (BENCH_MODEL_HDR|BENCH_MODEL_SYM|MODEL_W|MODEL_H|BACKBONE) ' "$ino"
