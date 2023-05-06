#!/bin/sh

set -ex

# Setup. (ins kernel module, create device file at /dev/hucdev)
sudo insmod huc-driver.ko
sudo ./mknoddev.sh hucdev
