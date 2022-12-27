#!/bin/bash

cd "`dirname "$0"`"

ARCH=`uname -m`
PLATFORM=`uname -s`

ulimit -s `ulimit -Hs`

if [ "$PLATFORM" = "Linux" ]; then
    export LD_LIBRARY_PATH=".:$LD_LIBRARY_PATH"
    exec ./Bombe
elif  [ "$PLATFORM" = "Darwin" ]; then
    export DYLD_LIBRARY_PATH=".:$DYLD_LIBRARY_PATH"
    exec ./Bombe
fi
