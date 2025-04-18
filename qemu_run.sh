#!/bin/bash

qemu-system-x86_64 \
    -m 36G \
    -kernel $1 \
    -initrd $2 \
    -append "root=/dev/vda rw console=ttyS0 loglevel=6 ignore_loglevel raid=noautodetect" \
    -drive file=rootfs.ext4,format=raw,if=virtio \
    -nographic \
    -serial mon:stdio 2>&1 | tee kernel.log

