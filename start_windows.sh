#!/bin/bash

SOCKET_PATH="/tmp/vserial"

# Remove any existing socket
rm -f $SOCKET_PATH

qemu-system-x86_64 \
  -name "Windows_SOCKS_Test" \
  -enable-kvm \
  -cpu host \
  -m 4G \
  -smp 4,sockets=1,cores=4,threads=1 \
  -drive file=windows.qcow2,format=qcow2 \
  -drive file=~/qemu/Win10_22H2_English_x64v1.iso,media=cdrom,index=1 \
  -drive file=~/qemu/virtio-win-0.1.266.iso,media=cdrom,index=2 \
  -boot menu=on \
  -device virtio-net,netdev=net0 \
  -netdev user,id=net0 \
  -device virtio-serial \
  -chardev socket,path=$SOCKET_PATH,server=on,wait=off,id=vserial0 \
  -device virtserialport,chardev=vserial0,name=vserial0,id=vserial0,port_name="COM1" \
  -vga virtio \
  -display gtk,gl=on \
  -usbdevice tablet
