// board_config.h - pin map and build-time options
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// First: several decisions below key off SDK macros, and some sources include
// this header before any SDK header.
#include "pico.h"

// ---------------------------------------------------------------------------
// JTAG pins. Default is an RP2350B (QFN-80) layout:
//   GPIO30 = pin 38 = TCK
//   GPIO31 = pin 39 = TDI
//   GPIO32 = pin 40 = TDO
//   GPIO33 = pin 42 = TMS
// ---------------------------------------------------------------------------
#ifndef PIN_TCK
#define PIN_TCK   30
#define PIN_TDI   31
#define PIN_TDO   32
#define PIN_TMS   33
#endif

// PIO on RP2350B sees a 32-GPIO window; base 16 covers GPIO16..47. RP2040's
// PIO reaches every GPIO directly and has no window, so the base is 0 there
// and PREL() below becomes the identity.
// Derived from the pin numbers alone -- deliberately NOT from an SDK macro.
// PICO_PIO_USE_GPIO_BASE lives in hardware/pio.h and NUM_BANK0_GPIOS is not
// visible from pico.h either; keying off either one here silently evaluated
// to 0, put the window at GPIO0-31, and left TDO/TMS on GPIO32/33 outside it.
// JTAG dead, no compile error. Pin numbers are always in scope, so:
//   any pin above 31 -> the window must start at 16 (covers GPIO16..47)
//   otherwise        -> base 0 (covers GPIO0..31), which is also the only
//                       option on RP2040, where PIO has no window at all
#if (PIN_TCK > 31) || (PIN_TDI > 31) || (PIN_TDO > 31) || (PIN_TMS > 31)
  #define PIO_GPIO_BASE 16
#else
  #define PIO_GPIO_BASE 0
#endif

_Static_assert(PIN_TCK >= PIO_GPIO_BASE && PIN_TCK < PIO_GPIO_BASE + 32 &&
               PIN_TDI >= PIO_GPIO_BASE && PIN_TDI < PIO_GPIO_BASE + 32 &&
               PIN_TDO >= PIO_GPIO_BASE && PIN_TDO < PIO_GPIO_BASE + 32 &&
               PIN_TMS >= PIO_GPIO_BASE && PIN_TMS < PIO_GPIO_BASE + 32,
               "JTAG pins fall outside the 32-GPIO PIO window");

// Relative pin index for DIRECT writes to the PINCTRL register, whose pin
// fields are relative to PIO_GPIO_BASE. Verified against pico-sdk 2.1.1:
// sm_config_set_*_pins() take ABSOLUTE GPIO numbers and pio_sm_set_config()
// subtracts the base itself, so PREL() is only for raw register writes.
#define PREL(g) ((g) - PIO_GPIO_BASE)

// ---------------------------------------------------------------------------
// Clocking
// ---------------------------------------------------------------------------
#define JTAG_SYS_CLK_KHZ  SYS_CLK_KHZ   // SDK default: 150000 / 125000
#define JTAG_MAX_TCK_HZ   12000000u   // clamp; PIO ceiling is clk_sys/12 = 12.5 MHz

// TDO idle bias. Bring-up diagnostic: if readings follow whichever pull is
// selected here, nothing is driving TDO. 0 = pull-down, 1 = pull-up.
#ifndef TDO_PULL_UP
#define TDO_PULL_UP 1
#endif
#define JTAG_MIN_TCK_HZ   1000u

// ---------------------------------------------------------------------------
// USB identity.  0x0403:0x6010 is FT2232C/D/H.
//   bcdDevice 0x0700 -> FT2232H  (high speed part, full MPSSE command set)
//   bcdDevice 0x0500 -> FT2232D  (full speed part, no 0x8A/0x8B/0x8C/0x8D)
// We enumerate as full-speed with 64-byte endpoints either way, because the
// RP2350 USB controller is full-speed only. Start with H; fall back to D only
// if the host rejects the 64-byte endpoints. See README.
// ---------------------------------------------------------------------------
// FTDI_SINGLE_CHANNEL: emulate an FT232H (one channel) instead of an FT2232H.
// Worth preferring when a known-good FT232H cable exists for the target tool --
// cloning an identity that already works beats guessing what the host accepts.
//   cmake -DSINGLE_CHANNEL=1 ..
#ifndef FTDI_SINGLE_CHANNEL
#define FTDI_SINGLE_CHANNEL 0
#endif

// FTDI_QUAD: emulate an FT4232H (four interfaces, 0x0403:0x6011) instead of
// an FT2232H. Required by Xilinx hw_server (Vivado), which only accepts
// FT4232H-class cables carrying a Xilinx EEPROM persona. Channel A keeps
// the MPSSE/JTAG logic; B/C/D answer as idle UARTs. Channel endpoints:
// A OUT 0x02/IN 0x81, B 0x04/0x84, C 0x06/0x86, D 0x08/0x88.
//   cmake -DQUAD=1 ..
#ifndef FTDI_QUAD
#define FTDI_QUAD 0
#endif
#if FTDI_QUAD && FTDI_SINGLE_CHANNEL
#error "QUAD and SINGLE_CHANNEL are mutually exclusive"
#endif

#define FTDI_VID          0x0403

#if FTDI_SINGLE_CHANNEL
#define FTDI_PID          0x6014      // FT232H
#ifndef FTDI_BCD_DEVICE
#define FTDI_BCD_DEVICE   0x0900
#endif
#elif FTDI_QUAD
#define FTDI_PID          0x6011      // FT4232H
#ifndef FTDI_BCD_DEVICE
#define FTDI_BCD_DEVICE   0x0800
#endif
#else
#define FTDI_PID          0x6010      // FT2232C/D/H
#ifndef FTDI_BCD_DEVICE
#define FTDI_BCD_DEVICE   0x0700
#endif
#endif

// Efinity lists cables by their USB product string. If your Trion dev kit
// reports something else, put that string here so Efinity treats this the
// same way.  "Dual RS232-HS" is the stock FT2232H description.
// Bump on every reflash during bring-up; shows up as the first two chars
// after "FT" in iSerial, so `lsusb -v | grep iSerial` confirms what is running.
#define FW_BUILD_TAG "C4"

#if FTDI_QUAD
#define FTDI_STR_MANUFACTURER "Xilinx"
#else
#define FTDI_STR_MANUFACTURER "FTDI"
#endif
#if FTDI_SINGLE_CHANNEL
// "Single RS232-HS" is the stock FT232H string. If Efinity is fussy, set this
// to the exact product string of a cable already known to work with it.
#ifndef FTDI_STR_PRODUCT
#define FTDI_STR_PRODUCT  "Single RS232-HS"
#endif
#elif FTDI_QUAD
// Persona of a Xilinx-cable FT4232H (strings observed live from the accepted
// HelloFPGA FT4232H, 2026-08-24 dual capture): hw_server rejects stock FTDI
// strings before any MPSSE traffic. Full EEPROM image served from
// eeprom_img.h, extracted from the same capture.
#ifndef FTDI_STR_PRODUCT
#define FTDI_STR_PRODUCT  "Adapt Device"
#endif
#define FTDI_STR_SERIAL_OVERRIDE "26SF041"
#else
#define FTDI_STR_PRODUCT  "Dual RS232-HS"
#endif

// Status bytes prefixed to every bulk IN packet.
#define FTDI_MODEM_STATUS 0x01
#define FTDI_LINE_STATUS  0x60   // THRE | TEMT

// ---------------------------------------------------------------------------
// Optional: map MPSSE ADBUS4..7 / ACBUS0..7 to real GPIOs (e.g. FPGA CRESET_N).
// Set to -1 for unused. Fill these in if Efinity toggles a reset line.
// ---------------------------------------------------------------------------
#define ADBUS_AUX_PINS { -1, -1, -1, -1 }   // ADBUS4,5,6,7
#define ACBUS_AUX_PINS { -1, -1, -1, -1, -1, -1, -1, -1 }

#endif
