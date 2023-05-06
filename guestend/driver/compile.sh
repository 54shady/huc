#!/usr/bin/env bash

cd /code
make M=$PWD modules -C /usr/src/linux
