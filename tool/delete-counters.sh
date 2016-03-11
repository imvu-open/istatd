#!/bin/sh
if [ "$1" = "" ]; then
    echo "usage: delete-counters.sh pattern [pattern]"
    exit 1
fi
case $1 in
    -*)
        echo "usage: delete-counters.sh pattern [pattern]"
        exit 1
        ;;
esac
echo "delete.pattern $*" | nc localhost 8112
