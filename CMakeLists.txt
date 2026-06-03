cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# ---------- Module identity (overridable per module variant) ---------------
# Pass -DPRODUCT_ID=0x... / -DHW_REVISION=N / -DFW_VERSION=0x... on the cmake
# command line to customise the firmware header for each module variant.
set(PRODUCT_ID    "0x12D1D4A0" CACHE STRING "Product ID embedded in fw_header_t")
set(HW_REVISION   "1"          CACHE STRING "Hardware revision embedded in fw_header_t")
set(FW_VERSION    "0x00010000" CACHE STRING "Firmware version 0xMMmmpp embedded in fw_header_t")

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Application sources
    Application/app/app.c
    Application/di/di_module.c
    Application/led/led_module.c
    Application/button/button_module.c
    Application/ksz8863/ksz8863.c
    Application/settings/settings.c
    Application/modbus/modbus_app.c
    Application/modbus/modbus_tcp_server.c
    Application/fw_header/fw_header.c
    # Third-party
    Application/third_party/nanomodbus/nanomodbus.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Application/app
    Application/di
    Application/led
    Application/button
    Application/ksz8863
    Application/settings
    Application/modbus
    Application/fw_header
    Application/third_party/nanomodbus
)

# Disable nanoMODBUS client side - we only need the slave/server.
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    NMBS_CLIENT_DISABLED=1
)

# Add project symbols (macros) -- firmware header identity fields.
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    FW_PRODUCT_ID=${PRODUCT_ID}
    FW_HW_REVISION=${HW_REVISION}
    FW_VERSION_VALUE=${FW_VERSION}
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# ---------- Post-build: generate app.bin with embedded fw_header_t ----------
# Requires Python 3.  gen_app_bin.py patches image_size and image_crc32 fields
# in the .fw_header section so the bootloader can read them without metadata.
find_program(PYTHON_EXECUTABLE NAMES python3 python)
if(PYTHON_EXECUTABLE)
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        COMMAND ${PYTHON_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/gen_app_bin.py
                --input   ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
                --output  ${CMAKE_BINARY_DIR}/app.bin
                --product-id  ${PRODUCT_ID}
                --hw-revision ${HW_REVISION}
                --fw-version  ${FW_VERSION}
        COMMENT "Generating app.bin with embedded firmware header"
    )
else()
    message(WARNING "Python not found -- app.bin will not be generated automatically")
endif()
