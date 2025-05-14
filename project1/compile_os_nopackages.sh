#!/bin/bash -x
sudo apt update; sudo apt-get install -y libncurses-dev bison flex

PROC=`nproc`
export CONCURRENCY_LEVEL=$PROC
export CONCURRENCYLEVEL=$PROC

cp /boot/config-$(uname -r) .config
#make menuconfig
make oldconfig

scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS

touch REPORTING-BUGS
make clean -j
make prepare
make -j$PROC
make modules -j$PROC

