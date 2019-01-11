#!/bin/bash
export BELA_LDLIBS="$BELA_LDLIBS -lkeys -L/root/spi-pru"
export BELA_CPPFLAGS="-I/root/spi-pru -std=c++14"
export COMMAND_ARGS="-H 0"

FILE=$1
BUILD="${FILE%.dsp}"

mkdir -p $BUILD
# move all the needed files in $BUILD
rsync -av static/* $BUILD/
# build : this will build the faust code into a render.cpp in $BUILD
faust2bela -gui $FILE
