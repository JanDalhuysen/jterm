#!/bin/sh

CC=cc
CFLAGS="-std=c99 -Wall -D_GNU_SOURCE"
SRC="src/*.c"
LFLAGS=""

OS=$(uname)
if [ $OS = "Linux" ]; then
    LFLAGS="-lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm"
elif [ $OS = "Darwin" ]; then
    SRC="src/sokol_mac.m"
    echo "Building $OS..."
    LFLAGS="-framework Cocoa -framework QuartzCore -framework Metal -framework MetalKit -lobjc"
else
    echo "$OS not supported"
    exit 1
fi

set -xe
$CC $CFLAGS $SRC -o jterm $LFLAGS

