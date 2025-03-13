# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "board_cfg.txt.S"
  "bootloader\\bootloader.bin"
  "bootloader\\bootloader.elf"
  "bootloader\\bootloader.map"
  "config\\sdkconfig.cmake"
  "config\\sdkconfig.h"
  "doorbell_demo.bin"
  "doorbell_demo.map"
  "esp-idf\\esptool_py\\flasher_args.json.in"
  "esp-idf\\mbedtls\\x509_crt_bundle"
  "flash_app_args"
  "flash_bootloader_args"
  "flash_project_args"
  "flasher_args.json"
  "join.aac.S"
  "ldgen_libraries"
  "ldgen_libraries.in"
  "open.aac.S"
  "project_elf_src_esp32.c"
  "ring.aac.S"
  "x509_crt_bundle.S"
  )
endif()
