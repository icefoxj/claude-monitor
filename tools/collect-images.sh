#!/usr/bin/env bash
# collect-images.sh <project-dir> <board> <version> <out-dir>
#
# Copies a built ESP-IDF project's images under their release names and
# builds the merged image with the project's own flash arguments, read from
# build/flasher_args.json (chip, flash mode/size/frequency, offsets). Used
# by CI for every board; works locally in an activated ESP-IDF shell too.
#
#   claude-monitor-<board>-<version>-merged.bin           everything, for offset 0x0
#   claude-monitor-<board>-<version>.bin                  the app
#   claude-monitor-<board>-<version>-bootloader.bin
#   claude-monitor-<board>-<version>-partition-table.bin
set -euo pipefail

PROJECT="$1"; BOARD="$2"; VERSION="$3"; OUT="$4"
B="$PROJECT/build"
FA="$B/flasher_args.json"
test -f "$FA" || { echo "no $FA: build the project first" >&2; exit 1; }

field() { python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(eval(sys.argv[2]))" "$FA" "$1"; }

CHIP="$(field "d['extra_esptool_args']['chip']")"
WRITE_ARGS="$(field "' '.join(d['write_flash_args'])")"
APP="$(field "d['app']['file']")";               APP_OFF="$(field "d['app']['offset']")"
BOOT="$(field "d['bootloader']['file']")";       BOOT_OFF="$(field "d['bootloader']['offset']")"
PT="$(field "d['partition-table']['file']")";    PT_OFF="$(field "d['partition-table']['offset']")"

mkdir -p "$OUT"
PREFIX="$OUT/claude-monitor-$BOARD-$VERSION"

# shellcheck disable=SC2086  # WRITE_ARGS is a list of flags on purpose
python3 -m esptool --chip "$CHIP" merge_bin -o "$PREFIX-merged.bin" $WRITE_ARGS \
    "$BOOT_OFF" "$B/$BOOT" "$PT_OFF" "$B/$PT" "$APP_OFF" "$B/$APP"
cp "$B/$APP"  "$PREFIX.bin"
cp "$B/$BOOT" "$PREFIX-bootloader.bin"
cp "$B/$PT"   "$PREFIX-partition-table.bin"

echo "$BOARD ($CHIP): offsets bootloader=$BOOT_OFF partition-table=$PT_OFF app=$APP_OFF; write_flash args: $WRITE_ARGS"
echo "app version embedded in the binary:"
grep -a -o -m1 'v[0-9][0-9A-Za-z.+-]*' "$B/$APP" | head -1 || true
ls -l "$OUT"
