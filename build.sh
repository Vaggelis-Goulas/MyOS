#!/bin/bash
# Build both the kernel image (build/floppy.img) and standalone userland
# ELF executables (build/user/*.elf) installed onto disk.img (FAT12).
#   ./build.sh            -> make all + disk (kernel + user + images)
#   ./build.sh <target>   -> make <target>  (e.g. clean, all, disk, user, test)
set -e
if [ $# -eq 0 ]; then
  exec make all disk
else
  exec make "$@"
fi