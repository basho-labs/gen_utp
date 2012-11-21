#!/bin/sh

set -e

VSN=d4685a

[ `basename $PWD` = c_src ] || cd c_src

case "$1" in
    clean)
        rm -rf libutp
        ;;

    *)
        test -f libutp/libutp.a && exit 0

        tar -xzf libutp-${VSN}.tar.gz
        ( set -x ; cd libutp && patch -p0 < ../debug-patch && make CXXFLAGS+="$DRV_CFLAGS" )
        ;;
esac
