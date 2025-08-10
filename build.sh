#!/bin/sh

CC=cc
CFLAGS="-std=c99 -Wall -D_GNU_SOURCE"
SRC="src/*.c"
LFLAGS=""

OS=$(uname)
if [ $OS = "Linux" ]; then
    LFLAGS="-lX11 -lXi -lXcursor -lGL -ldl -lpthread -lm"
else
    echo "$OS not supported"
fi

set -xe
$CC $CFLAGS $SRC -o jterm $LFLAGS

