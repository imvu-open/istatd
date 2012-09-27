#!/bin/bash
set -e
set -o nounset

if [ -x /usr/bin/python2 ]; then
    echo "Adding symlink to python2 into PATH."
    ln -sf /usr/bin/python2 `pwd`/python
    export PATH=`pwd`":$PATH"
fi

echo "calling ./configure"
./configure
echo "calling make -j4"
make -j4
echo "setting up in /var/tmp"
mkdir /var/tmp/istatd-quickstart
mkdir /var/tmp/istatd-quickstart/store
mkdir /var/tmp/istatd-quickstart/settings
ln -sf `pwd`/files /var/tmp/istatd-quickstart/files
echo "istatd starting up -- hit localhost:18011 in a browser to see it in action"
bin/istatd --config quickstart.cfg --user `logname`
