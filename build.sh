#!/bin/bash

sudo rm /var/log/istatd.log
make OPT=
sudo /etc/init.d/istatd stop
sudo cp bin/istatd /usr/bin/istatd
sudo /etc/init.d/istatd start
