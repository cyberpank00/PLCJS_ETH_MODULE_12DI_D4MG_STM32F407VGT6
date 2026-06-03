#!/usr/bin/env python3
"""
gen_app_bin.py — post-build helper that patches the fw_header_t in app.bin.

After the linker places fw_header.c at offset 0x200 from the start of FLASH,
the header already contains magic, product_id, hw_revision, and fw_version
(baked in by the compiler via -D flags).  The two remaining fields that cannot
be known at compile time are:

    image_size    — total binary size in bytes
    image_crc32   — CRC32 (zlib/IEEE) of the whole binary with this field = 0

This script reads the raw .bin, fills those two fields, and writes app.bin.

The bootloader reads the header at APP_FLASH_BASE + 0x200 to verify product
identity before accepting or running the image.

fw_header_t layout (packed, 36 bytes):
  offset  field                size
  0x00    magic                4  (FW_IMAGE_MAGIC = 0x504C434A "PLCJ")
  0x04    product_id           4
  0x08    hw_revision          2
  0x0A    reserved0            2
  0x0C    fw_version           4
  0x10    image_size           4  <-- patched here
  0x14    image_crc32          4  <-- patched here (CRC with this field = 0)
  0x18    vector_table_offset  4
  0x1C    reserved1[2]         8
  total                       36
"""

import argparse
import struct
import sys
import zlib

FW_IMAGE_MAGIC   = 0x504C434A      # "PLCJ"
FW_HEADER_OFFSET = 0x200           # APP_FLASH_BASE + 0x200

HDR_SIZE         = 36              # sizeof(fw_header_t)
OFF_MAGIC        = FW_HEADER_OFFSET + 0x00
OFF_PRODUCT_ID   = FW_HEADER_OFFSET + 0x04
OFF_HW_REVISION  = FW_HEADER_OFFSET + 0x08
OFF_FW_VERSION   = FW_HEADER_OFFSET + 0x0C
OFF_IMAGE_SIZE   = FW_HEADER_OFFSET + 0x10
OFF_IMAGE_CRC32  = FW_HEADER_OFFSET + 0x14


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFF_FFFF


def read_u32le(data: bytearray, off: int) -> int:
    return struct.unpack_from('<I', data, off)[0]


def write_u32le(data: bytearray, off: int, value: int) -> None:
    struct.pack_into('<I', data, off, value & 0xFFFF_FFFF)


def main() -> None:
    p = argparse.ArgumentParser(
        description='Patch fw_header_t in a raw STM32 binary for OTA update.')
    p.add_argument('--input',        required=True, help='Input raw .bin file')
    p.add_argument('--output',       required=True, help='Output app.bin file')
    p.add_argument('--product-id',   required=True, help='Product ID (hex or dec)')
    p.add_argument('--hw-revision',  required=True, help='HW revision (dec)')
    p.add_argument('--fw-version',   required=True, help='FW version 0xMMmmpp')
    args = p.parse_args()

    product_id  = int(args.product_id,  0)
    hw_revision = int(args.hw_revision, 0)
    fw_version  = int(args.fw_version,  0)

    with open(args.input, 'rb') as f:
        data = bytearray(f.read())

    if len(data) < FW_HEADER_OFFSET + HDR_SIZE:
        sys.exit(f'ERROR: binary too small ({len(data)} bytes) — '
                 f'need at least {FW_HEADER_OFFSET + HDR_SIZE}')

    # Sanity-check that the magic is already in place (placed by the compiler).
    magic = read_u32le(data, OFF_MAGIC)
    if magic != FW_IMAGE_MAGIC:
        sys.exit(
            f'ERROR: magic at 0x{OFF_MAGIC:03X} = 0x{magic:08X}, '
            f'expected 0x{FW_IMAGE_MAGIC:08X}.\n'
            f'The linker section .fw_header may not be at offset 0x{FW_HEADER_OFFSET:X}.\n'
            f'Check STM32F407XX_FLASH.ld and that fw_header.c was compiled.')

    baked_product_id  = read_u32le(data, OFF_PRODUCT_ID)
    baked_hw_revision = struct.unpack_from('<H', data, OFF_HW_REVISION)[0]
    baked_fw_version  = read_u32le(data, OFF_FW_VERSION)

    if baked_product_id != product_id:
        sys.exit(
            f'ERROR: product_id in binary (0x{baked_product_id:08X}) '
            f'!= CMake arg (0x{product_id:08X})')
    if baked_hw_revision != hw_revision:
        sys.exit(
            f'ERROR: hw_revision in binary ({baked_hw_revision}) '
            f'!= CMake arg ({hw_revision})')
    if baked_fw_version != fw_version:
        print(
            f'WARNING: fw_version in binary (0x{baked_fw_version:08X}) '
            f'!= CMake arg (0x{fw_version:08X})')

    # Patch image_size
    image_size = len(data)
    write_u32le(data, OFF_IMAGE_SIZE, image_size)

    # Patch image_crc32: compute CRC with the CRC field zeroed
    write_u32le(data, OFF_IMAGE_CRC32, 0)
    image_crc32 = crc32(data)
    write_u32le(data, OFF_IMAGE_CRC32, image_crc32)

    with open(args.output, 'wb') as f:
        f.write(data)

    print(f'app.bin  size=0x{image_size:X} ({image_size} bytes)'
          f'  crc32=0x{image_crc32:08X}'
          f'  product_id=0x{product_id:08X}'
          f'  hw_rev={hw_revision}'
          f'  fw_version=0x{fw_version:08X}')


if __name__ == '__main__':
    main()
