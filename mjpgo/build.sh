#!/bin/bash
set -e

mkdir -p bin

SRC_FILES="
    src/udp_common.c
    src/udp_sender.c
    src/udp_receiver.c
    src/video_capturer.c
    src/frame_pipe.c
    src/frame_recorder.c
    src/mjpgo.c
"

CFLAGS="-Wall -Wextra -O2 -Iinclude"
LDFLAGS="-lm -lavformat -lavcodec -lavutil"

echo "Building mjpgo..."
gcc $CFLAGS $SRC_FILES -o bin/mjpgo $LDFLAGS

echo "Build complete: bin/mjpgo"
