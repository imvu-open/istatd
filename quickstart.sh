#!/bin/bash
set -e
set -o nounset

if [ -x /usr/bin/python2 ]; then
    echo "Adding symlink to python2 into PATH."
    ln -s /usr/bin/python2 `pwd`/python
    export PATH=`pwd`":$PATH"
fi

echo "calling ./configure"
./configure
echo "calling make -j4"
make -j4
echo "setting up in /var/tmp"
mkdir /var/tmp/istatd-quickstart
ln -sf `pwd`/files /var/tmp/files
echo "istatd starting up -- hit localhost:18011 in a browser to see it in action"
bin/istatd --settings quickstart.cfg --user `logname`
