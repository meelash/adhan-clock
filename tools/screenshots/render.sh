#!/bin/sh
# Regenerate docs/screens/*.png from the firmware's UI code.
# Needs a host C compiler and Python 3 with Pillow.
set -e
here=$(cd "$(dirname "$0")" && pwd)
fw="$here/../../firmware"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -O1 -D_DEFAULT_SOURCE \
    -I"$here/stub" -I"$fw/include" -I"$fw/src" -I"$fw/lib/adhan/include" -I"$fw/lib/adhan" \
    "$here/render_screens.c" \
    "$fw/src/ui/screens.c" "$fw/src/ui/fonts.c" "$fw/src/ui/display.c" "$fw/src/ui/theme.c" \
    "$fw/src/prayer/prayer_times.c" "$fw/src/prayer/hijri.c" "$fw/src/app/timeutil.c" \
    "$fw"/lib/adhan/*.c -lm -o "$tmp/render"

# libadhan uses localtime(), which is UTC on the Pico
TZ=UTC "$tmp/render" "$tmp" > /dev/null
python3 "$here/make_pngs.py" "$tmp" "$here/../../docs/screens"
echo "Wrote $(ls "$here/../../docs/screens" | wc -l) screenshots to docs/screens/"
