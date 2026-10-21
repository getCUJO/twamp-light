#!/bin/bash
set -euo pipefail
VER=$1
ARCH=${2:-$(dpkg --print-architecture)}
PKG=twamp-light_${VER}_${ARCH}
mkdir -p $PKG/usr/local/bin
mkdir -p $PKG/lib/systemd/system
mkdir -p $PKG/usr/local/lib
mkdir $PKG/DEBIAN
cp ../debian/control.txt $PKG/DEBIAN/control
cp ../debian/triggers.txt $PKG/DEBIAN/triggers
cp ../debian/postinst.sh $PKG/DEBIAN/postinst

sed -i -e "s/^Version: .*/Version: $VER/" -e "s/^Architecture: .*/Architecture: $ARCH/" $PKG/DEBIAN/control

cp twamp-light-client $PKG/usr/local/bin
cp qoo-c/src/libqoo.so $PKG/usr/local/lib/libqoo.so
cp twamp-light-server $PKG/usr/local/bin
cp ../systemd/twamp-light-server.service $PKG/lib/systemd/system

dpkg-deb --build $PKG
