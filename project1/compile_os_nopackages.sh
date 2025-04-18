#!/bin/bash -x
sudo apt update
sudo apt-get install -y libncurses-dev bison flex

PROC=`nproc`
export CONCURRENCY_LEVEL=$PROC
export CONCURRENCYLEVEL=$PROC

cp /boot/config-$(uname -r) .config
#make menuconfig
make oldconfig

scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS

scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS

make menuconfig

touch REPORTING-BUGS
make clean -j
make prepare
make CC='ccache gcc' -j$PROC
make CC='ccache gcc' modules -j$PROC
#sudo make modules_install
#sudo make install

#y="5.15.168"
#
#sudo cp ./arch/x86/boot/bzImage /boot/vmlinuz-$y
#sudo cp System.map /boot/System.map-$y
#sudo cp .config /boot/config-$y
#sudo update-initramfs -c -k $y
#
#sudo update-grub
