#!/bin/sh
# Builds and runs the native test harness (no Wii toolchain needed). Output goes to tools/out/.
#   sh tools/run_tests.sh              map checks, screenshots, bot playthroughs
#   sh tools/run_tests.sh shots [m]    screenshots only (one mission)
#   sh tools/run_tests.sh bots [m]     bot playthroughs only
#   sh tools/run_tests.sh reel 3       renders tools/out/reel/preview.mp4 of mission 3 (needs ffmpeg)
set -e
cd "$(dirname "$0")/.."
python3 tools/maps.py >/dev/null
mkdir -p tools/out/reel
cc -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation \
   source/game.c source/sim.c source/level.c source/draw.c source/hud.c source/sprites.c source/gfx.c \
   source/audio.c source/music.c source/speech.c tools/test.c -lm -o tools/out/gtest
tools/out/gtest tools/out ${1:-all} $2
python3 -c "
import glob, os
from PIL import Image
for p in glob.glob('tools/out/*.ppm'):
    Image.open(p).save(p[:-4] + '.png'); os.remove(p)
" 2>/dev/null || echo "(PNG export skipped: needs python3 + Pillow)"
if [ "$1" = "reel" ]; then
    ffmpeg -loglevel error -y -framerate 30 -i tools/out/reel/f%04d.ppm -i tools/out/reel/music.wav -c:v libx264 -preset veryfast -pix_fmt yuv420p -crf 20 -c:a aac -b:a 160k -shortest tools/out/reel/preview.mp4
    rm -f tools/out/reel/*.ppm
    echo "preview: tools/out/reel/preview.mp4"
fi
