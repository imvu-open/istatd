#!/bin/bash

ssh istatd-master "mkdir -p files" || exit 1
echo -n "Uploading GUI files to istatd-master "
date
rsync -avh files/* istatd-master:files/
echo -n "Deploying files to istatd-master "
date
ssh -t istatd-master "sudo cp -v files/* /home/webadmin/istatd/files/"
echo -n "Done "
date
