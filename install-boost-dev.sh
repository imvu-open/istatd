#!/bin/sh
apt-get install `apt-cache search libboost | grep -- -dev | grep -v '[12]\.[0-9]' | awk '{ print $1; }'`
