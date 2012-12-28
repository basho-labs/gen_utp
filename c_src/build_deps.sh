#!/bin/sh

set -e

VSN=d4685a

[ `basename $PWD` = c_src ] || cd c_src

case "$1" in
    clean)
        make clean
        rm -rf libutp
        ;;

    *)
        if [ ! -f libutp/libutp.a ]; then
            [ -d libutp ] || tar -xzf libutp-${VSN}.tar.gz
            ( cd libutp && make CXXFLAGS+="$DRV_CFLAGS" )
        fi
        make all
        ;;
esac
