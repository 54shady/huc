#!/usr/bin/env bash

mkdir -p rootfs debbuild/DEBIAN build
cd build && \
../configure \
	--target-list="x86_64-softmmu" \
	--enable-debug \
	--disable-docs \
	--disable-capstone \
	--disable-nettle \
	--disable-gnutls \
	--disable-gcrypt \
	--extra-cflags="-O0" \
	--enable-trace-backends=ftrace

make all -j$(nproc) CONFIG_NEWDEV=y CONFIG_VIRTFS=y CONFIG_VIRTIO_9P=y

make DESTDIR=/code/rootfs sharedir="/usr/share/qemu" datadir="/usr/share/qemu" install
cp -rf /code/rootfs/* /code/debbuild/
dpkg-deb -v --build /code/debbuild /code/newdev-qemu.deb
