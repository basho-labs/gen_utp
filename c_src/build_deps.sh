#!/bin/sh

set -e

# libutp versions come from https://github.com/bittorrent/libutp.
# To retrieve a new version:
#
#  git clone git@github.com:bittorrent/libutp.git
#  cd libutp
#  VSN=`git log --pretty=format:'%h' | head -1`
#  git archive --prefix=./libutp/ --format=tar HEAD | \
#    gzip > libutp-${VSN}.tar.gz
#
# where the shell variable VSN ends up being the commit sha of HEAD of the
# libutp repo, the value of which should also be set in the VSN var below.
#
VSN=f904d1b

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
