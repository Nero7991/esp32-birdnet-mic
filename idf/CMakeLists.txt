# ESP-IDF build of the Tailscale fork (XIAO ESP32-S3 Sense). The sketch sources stay in
# ../esp32-birdnet-mic so upstream changes merge without copies.
cmake_minimum_required(VERSION 3.16)
# MicroLink (Tailscale client) and its wireguard_lwip dependency.
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/libs/microlink/components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Serial on the USB-JTAG port, as in the upstream XIAO_ESP32S3 build (USBMode=hwcdc, CDCOnBoot).
# These must reach every component, since the core defines the Serial object.
idf_build_set_property(COMPILE_OPTIONS "-DARDUINO_USB_MODE=1" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DARDUINO_USB_CDC_ON_BOOT=1" APPEND)
project(birdnet-mic-ts)
