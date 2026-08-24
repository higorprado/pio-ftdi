cmake_minimum_required(VERSION 3.13)

# RP2350B by default -- an unqualified `cmake ..` reproduces the working
# configuration. For RP2040:
#   cmake -DRP2040=1 -DJTAG_PINS="6;7;8;9" ..
if(RP2040)
    set(PICO_PLATFORM rp2040 CACHE STRING "")
    if(NOT DEFINED PICO_BOARD)
        set(PICO_BOARD pico CACHE STRING "")
    endif()
else()
    set(PICO_PLATFORM rp2350 CACHE STRING "")
    if(NOT DEFINED PICO_BOARD)
        set(PICO_BOARD rp2350b CACHE STRING "")
    endif()
endif()
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

include(pico_sdk_import.cmake)

project(pio_ftdi C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(pio_ftdi
    src/main.c
    src/jtag_pio.c
    src/mpsse.c
    src/ftdi_usb.c
)

pico_generate_pio_header(pio_ftdi ${CMAKE_CURRENT_LIST_DIR}/src/jtag.pio)

target_include_directories(pio_ftdi PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

target_link_libraries(pio_ftdi
    pico_stdlib
    pico_unique_id
    hardware_pio
    hardware_clocks
    hardware_gpio
    hardware_flash
    tinyusb_device
)

# Build as an FT2232D instead (full-speed part, no 0x8A/0x8B/0x8C/0x8D):
#   cmake -DFTDI_AS_D=1 ..
# Bring-up: cmake -DTDO_PULLDOWN=1 .. to bias TDO low instead of high.
if(TDO_PULLDOWN)
    target_compile_definitions(pio_ftdi PRIVATE TDO_PULL_UP=0)
endif()

# Emulate a single-channel FT232H (0403:6014) instead of an FT2232H.
#   cmake -DSINGLE_CHANNEL=1 ..
# Optionally match a specific known-good cable's product string:
#   cmake -DSINGLE_CHANNEL=1 -DPRODUCT_STRING="Vaaman Prog" ..
# Drive an external FPGA board from header pins instead of the on-board Trion:
#   cmake -DALT_PINS=1 ..     TCK=GPIO9 TDI=GPIO8 TDO=GPIO7 TMS=GPIO6
# JTAG pinout, order TCK TDI TDO TMS. Overrides whatever the board file (or
# board_config.h) sets, so a board default never blocks a one-off wiring.
if(JTAG_PINS)
    list(GET JTAG_PINS 0 _tck)
    list(GET JTAG_PINS 1 _tdi)
    list(GET JTAG_PINS 2 _tdo)
    list(GET JTAG_PINS 3 _tms)
    target_compile_definitions(pio_ftdi PRIVATE
        PIN_TCK=${_tck} PIN_TDI=${_tdi} PIN_TDO=${_tdo} PIN_TMS=${_tms})
    message(STATUS "JTAG pins: TCK=${_tck} TDI=${_tdi} TDO=${_tdo} TMS=${_tms}")
else()
    message(STATUS "JTAG pins: from board file or board_config.h defaults")
endif()

if(BUILD_TAG)
    target_compile_definitions(pio_ftdi PRIVATE FW_BUILD_TAG="${BUILD_TAG}")
endif()

# Retarget the JTAG pins without editing sources, e.g. back to the on-board
# Trion:  cmake -DJTAG_PINS="30;31;32;33" ..   (order: TCK TDI TDO TMS)
if(JTAG_PINS)
    list(GET JTAG_PINS 0 _tck)
    list(GET JTAG_PINS 1 _tdi)
    list(GET JTAG_PINS 2 _tdo)
    list(GET JTAG_PINS 3 _tms)
    target_compile_definitions(pio_ftdi PRIVATE
        PIN_TCK=${_tck} PIN_TDI=${_tdi} PIN_TDO=${_tdo} PIN_TMS=${_tms})
    message(STATUS "JTAG pins: TCK=${_tck} TDI=${_tdi} TDO=${_tdo} TMS=${_tms}")
endif()

if(SINGLE_CHANNEL)
    target_compile_definitions(pio_ftdi PRIVATE FTDI_SINGLE_CHANNEL=1)
endif()

# Emulate an FT4232H (four interfaces, PID 0x6011) instead of an FT2232H.
# Required by Xilinx hw_server (Vivado); see the Vivado section in README.md.
#   cmake -DQUAD=1 ..
if(QUAD)
    target_compile_definitions(pio_ftdi PRIVATE FTDI_QUAD=1)
endif()

if(PRODUCT_STRING)
    target_compile_definitions(pio_ftdi PRIVATE FTDI_STR_PRODUCT="${PRODUCT_STRING}")
endif()

if(FTDI_AS_D)
    target_compile_definitions(pio_ftdi PRIVATE FTDI_BCD_DEVICE=0x0500)
endif()

pico_add_extra_outputs(pio_ftdi)