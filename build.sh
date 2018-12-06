#!/bin/bash
export BELA_LDLIBS="$BELA_LDLIBS -lkeys -L/root/spi-pru"
export BELA_CPPFLAGS="-I/root/spi-pru"
export COMMAND_ARGS="-H 0"
export ARCHFILE=faust/architecture/bela.cpp

FILE=vfl.dsp
BUILD="${FILE%.dsp}"

mkdir -p $BUILD
# move all the needed files in $BUILD
rsync -av static/* $BUILD/
# build : this will build the faust code into a render.cpp in $BUILD and then
# copy all of the content of $BUILD to the board and actually build the C++ project
faust2bela -tobela -gui vfl.dsp
