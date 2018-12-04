#!/bin/bash
export BELA_LDLIBS="$BELA_LDLIBS -lkeys -L/root/spi-pru"
export BELA_CPPFLAGS="-I/root/spi-pru -Wno-overloaded-virtual" 
export COMMAND_ARGS="-H 0"
export ARCHFILE=faust/architecture/bela.cpp

FILE=vfl.dsp
BUILD="${FILE%.dsp}"

mkdir -p $BUILD
rsync -av static/* $BUILD/
/Users/giulio/faust/tools/faust2appls/faust2bela -tobela -gui vfl.dsp
