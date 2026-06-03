/**
 * @file  fw_header.c
 * @brief Firmware image header instance placed at APP_FLASH_BASE + 0x200.
 *
 * The linker script pads .isr_vector to 0x200 bytes and then keeps .fw_header
 * immediately after, so g_fw_header lands at exactly the expected offset.
 *
 * product_id, hw_revision, and fw_version are set at compile time by CMake.
 * image_size and image_crc32 start as 0 and are patched by the post-build
 * script tools/gen_app_bin.py before the binary is used for OTA update.
 */

#include "fw_header.h"

const fw_header_t g_fw_header
    __attribute__((section(".fw_header")))
    __attribute__((used)) =
{
    .magic               = FW_IMAGE_MAGIC,
    .product_id          = FW_PRODUCT_ID,
    .hw_revision         = (uint16_t)FW_HW_REVISION,
    .reserved0           = 0u,
    .fw_version          = FW_VERSION_VALUE,
    .image_size          = 0u,   /* filled by gen_app_bin.py */
    .image_crc32         = 0u,   /* filled by gen_app_bin.py */
    .vector_table_offset = 0u,
    .reserved1           = {0u, 0u},
};
