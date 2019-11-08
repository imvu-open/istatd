#!/bin/sh
set -e

DEPS='qpdf curl ca-certificates build-essential make gcc g++ git python python-dev libboost-all-dev psmisc file autoconf autotools-dev automake jq'
NPROC="$(grep processor /proc/cpuinfo 2>/dev/null | wc -l)"

apt-get install -qq -y --no-install-recommends $DEPS

if [ ! -d /tmp/libstatgrab ]; then
    git clone --depth=1 -q https://github.com/imvu-open/libstatgrab /tmp/libstatgrab
    cd /tmp/libstatgrab
    ./configure --prefix=/usr
    make -j${NPROC-1}
    make install
fi
