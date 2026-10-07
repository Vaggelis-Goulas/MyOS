#!/usr/bin/env python3
"""Build a 1.44MB FAT12 floppy image with test files for MyOS.

Usage: python3 tools/build_fat12.py <out.img> [extra_host_file ...]

Extra host files (e.g. build/user/*.elf native executables) are stored
under their upper-cased basename (HELLO.ELF, ARGS.ELF, INIT.ELF, ...).
Kernel image itself lives on build/floppy.img; userland ELFs go here
on disk.img (IDE) so the shell exec/run commands can load them.
"""

import struct
import sys
import os

SPT = 18
HEADS = 2
TOTAL_SECTORS = 2880
BYTES_PER_SECTOR = 512
RESERVED = 1
NUM_FATS = 2
ROOT_ENTRIES = 224
SECTORS_PER_FAT = 9
MEDIA = 0xF0
VOLUME_LABEL = b"MYOS      "

ROOT_SECTOR = RESERVED + NUM_FATS * SECTORS_PER_FAT
ROOT_SECTORS = (ROOT_ENTRIES * 32 + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
DATA_SECTOR = ROOT_SECTOR + ROOT_SECTORS


def build_bpb():
    b = bytearray(512)
    b[0:3] = b'\xEB\x3C\x90'
    b[3:11] = b'MYOSBOOT'
    struct.pack_into('<H', b, 11, BYTES_PER_SECTOR)
    b[13] = 1                              # sectors per cluster
    struct.pack_into('<H', b, 14, RESERVED)
    b[16] = NUM_FATS
    struct.pack_into('<H', b, 17, ROOT_ENTRIES)
    struct.pack_into('<H', b, 19, TOTAL_SECTORS)
    b[21] = MEDIA
    struct.pack_into('<H', b, 22, SECTORS_PER_FAT)
    struct.pack_into('<H', b, 24, SPT)
    struct.pack_into('<H', b, 26, HEADS)
    struct.pack_into('<I', b, 28, 0)       # hidden sectors
    struct.pack_into('<I', b, 32, 0)       # total sectors 32
    b[36] = 0x00                           # drive number
    b[37] = 0x00                           # reserved
    b[38] = 0x29                           # boot signature
    struct.pack_into('<I', b, 39, 0x12345678)  # volume id
    b[43:54] = VOLUME_LABEL
    b[54:62] = b'FAT12   '
    return bytes(b)


def short_name(name):
    base, _, ext = name.partition('.')
    base = base.upper()[:8].ljust(8).encode()
    ext = ext.upper()[:3].ljust(3).encode()
    return base + ext


def dir_entry(name, first_cluster, size, ctime=0x4000, cdate=0x21):
    return (
        short_name(name) +
        bytes([0, 0, 0]) +                # attrs, reserved, tenths
        struct.pack('<H', ctime) +        # create time
        struct.pack('<H', cdate) +        # create date
        struct.pack('<H', cdate) +        # access date
        struct.pack('<H', 0) +            # first cluster high
        struct.pack('<H', ctime) +        # write time
        struct.pack('<H', cdate) +        # write date
        struct.pack('<H', first_cluster & 0xFFFF) +
        struct.pack('<I', size)
    )


def fat_set12(fat, cluster, value):
    """Store a 12-bit FAT12 entry.

    Entry `n` lives at byte n + n//2 and is split as

        n even:  low 8 bits in fat[off], high 4 bits in fat[off+1] & 0xF0
        n odd:   low 4 bits in fat[off] & 0xF0, high 8 bits in fat[off+1]

    which is exactly what C/fs.c fat_read_entry() reads back.
    """
    off = cluster + cluster // 2
    if cluster & 1:
        fat[off] = (fat[off] & 0x0F) | ((value & 0x0F) << 4)
        fat[off + 1] = (value >> 4) & 0xFF
    else:
        fat[off] = value & 0xFF
        fat[off + 1] = (fat[off + 1] & 0xF0) | ((value >> 8) & 0x0F)


def main():
    out = sys.argv[1]
    files = [
        ("HELLO.TXT", b"Hello from MyOS!\nThis file is read from the FAT12 disk.\n"),
        ("TEST.TXT", b"This is a test file for MyOS Filesystem version 1.\n"),
        ("README", b"MyOS custom operating system\nFAT12 filesystem test disk\n"),
    ]

    # Standalone userland executables built by `make user`
    # (build/user/*.elf linked at USER_BASE). Stored by basename so the
    # kernel shell can `exec HELLO.ELF ...` with CLI args.
    for host_path in sys.argv[2:]:
        with open(host_path, 'rb') as f:
            data = f.read()
        fat_name = os.path.basename(host_path).upper()
        # FAT 8.3: short_name() truncates, but warn on overly long names.
        if len(fat_name) > 12:
            print(f'warning: truncating {fat_name} to 8.3', file=sys.stderr)
        files.append((fat_name, data))

    img = bytearray(TOTAL_SECTORS * BYTES_PER_SECTOR)
    bpb = build_bpb()
    img[:len(bpb)] = bpb

    fat = bytearray(SECTORS_PER_FAT * BYTES_PER_SECTOR)
    fat[0] = MEDIA
    fat[1] = 0xFF
    fat[2] = 0xFF

    root = bytearray(ROOT_ENTRIES * 32)
    cluster = 2
    idx = 0
    for name, data in files:
        nclusters = (len(data) + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
        root[idx * 32: idx * 32 + 32] = dir_entry(name, cluster, len(data))
        idx += 1
        for k in range(nclusters):
            cur = cluster + k
            nxt = cluster + k + 1 if k < nclusters - 1 else 0xFFF
            fat_set12(fat, cur, nxt)
        data_start = (DATA_SECTOR + (cluster - 2)) * BYTES_PER_SECTOR
        img[data_start:data_start + len(data)] = data
        cluster += nclusters

    for fatno in range(NUM_FATS):
        s = (RESERVED + fatno * SECTORS_PER_FAT) * BYTES_PER_SECTOR
        img[s:s + len(fat)] = fat

    r = ROOT_SECTOR * BYTES_PER_SECTOR
    img[r:r + len(root)] = root

    with open(out, 'wb') as f:
        f.write(bytes(img))
    print(f'Wrote {out}: {len(img)} bytes, files: {[n for n, _ in files]}')


if __name__ == '__main__':
    main()