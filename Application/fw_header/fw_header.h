/**
 * @file  fw_header.h
 * @brief Firmware image header embedded at APP_FLASH_BASE + 0x200.
 *
 * The header is placed in the dedicated linker section ".fw_header" which is
 * located at exactly 0x200 bytes from the start of FLASH (right after the
 * vector table padding).  The bootloader reads this header to verify product
 * identity before accepting or running the image.
 *
 * product_id, hw_revision, and fw_version are baked in at compile time via
 * CMake -D flags (FW_PRODUCT_ID, FW_HW_REVISION, FW_VERSION_VALUE).
 * image_size and image_crc32 are filled in by tools/gen_app_bin.py after the
 * binary is produced.
 *
 * Layout must match fw_header_t in the bootloader's app_validate.h exactly.
 */
#ifndef FW_HEADER_H
#define FW_HEADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Magic identifying a valid PLCJ firmware image ("PLCJ" in ASCII). */
#define FW_IMAGE_MAGIC          0x504C434Au

/* Byte offset from APP_FLASH_BASE where the header is located.
 * Must match FW_HEADER_OFFSET in the bootloader's app_validate.h. */
#define FW_HEADER_OFFSET        0x200u

/**
 * Firmware image header (packed, 36 bytes total).
 * Placed at APP_FLASH_BASE + FW_HEADER_OFFSET in the binary.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;               /* FW_IMAGE_MAGIC ("PLCJ")              */
    uint32_t product_id;          /* Module product identifier            */
    uint16_t hw_revision;         /* Hardware revision                    */
    uint16_t reserved0;
    uint32_t fw_version;          /* 0xMMmmpp (major/minor/patch)         */
    uint32_t image_size;          /* Total image size in bytes            */
    uint32_t image_crc32;         /* CRC32 of binary with this field = 0  */
    uint32_t vector_table_offset; /* Offset of vector table (always 0)    */
    uint32_t reserved1[2];
} fw_header_t;

/* Compile-time defaults — overridden via CMake -D flags. */
#ifndef FW_PRODUCT_ID
#define FW_PRODUCT_ID   0x12D1D4A0u
#endif
#ifndef FW_HW_REVISION
#define FW_HW_REVISION  1u
#endif
#ifndef FW_VERSION_VALUE
#define FW_VERSION_VALUE 0x00010000u  /* 1.0.0 */
#endif

/** The single header instance placed in the .fw_header linker section. */
extern const fw_header_t g_fw_header;

#ifdef __cplusplus
}
#endif

#endif /* FW_HEADER_H */
