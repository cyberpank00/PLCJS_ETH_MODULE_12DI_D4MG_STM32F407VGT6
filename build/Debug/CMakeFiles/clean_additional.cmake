# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.map"
  )
endif()
