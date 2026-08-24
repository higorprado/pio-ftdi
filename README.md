# pio-ftdi

FTDI FT232H emulation on RP2040/RP2350 — program and debug FPGAs from a bare
Pico, no JTAG cable needed.

The MCU enumerates as an FT232H (`0403:6014`), implements enough of the FTDI
vendor protocol and MPSSE command set for a host to drive JTAG through it, and
shifts the bits with a single PIO state machine. Vendor tools talk to it as if
it were a real FTDI adapter.

Useful two ways: as a standalone adapter on a bare Pico, or as firmware for an
MCU already sitting next to an FPGA on your own board.

> **Tested only with Efinix FPGAs** — Trion T4 and T120, via Efinity and
> OpenOCD. It is standard MPSSE, so other vendors' tools may work, but nobody
> has tried.

## Build

Needs pico-sdk 2.0+ (`pio_set_gpio_base` does not exist in 1.x) and an
`arm-none-eabi` toolchain.

```sh
git clone -b 2.1.1 --depth 1 https://github.com/raspberrypi/pico-sdk
cd pico-sdk && git submodule update --init --depth 1 lib/tinyusb && cd ..
export PICO_SDK_PATH=$PWD/pico-sdk

cd pio-ftdi
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .
mkdir build && cd build
cmake -DSINGLE_CHANNEL=1 ..
make -j$(nproc)
```

The tinyusb submodule is **required** — without it the link fails on missing
`tud_*` symbols. Output is `build/pio_ftdi.uf2`; RP2040 boards mount as
`RPI-RP2`, RP2350 as `RP2350`.

### Configurations

```sh
cmake -DSINGLE_CHANNEL=1 ..                                    # RP2350B, pins 30/31/32/33
cmake -DRP2040=1 -DPICO_BOARD=pico_jtag -DSINGLE_CHANNEL=1 ..  # Pico,   pins 1/2/3/4
cmake -DPICO_BOARD=pico2_jtag -DSINGLE_CHANNEL=1 ..            # Pico 2, pins 1/2/3/4
cmake -DPICO_BOARD=zen -DSINGLE_CHANNEL=1 ..                   # RP2350B + Trion T4
cmake -DSINGLE_CHANNEL=1 -DJTAG_PINS="20;19;18;17" ..          # RP2350B, other pins
cmake -DRP2040=1 -DSINGLE_CHANNEL=1 -DJTAG_PINS="6;7;8;9" ..   # RP2040

A board file can carry both the chip variant and a default pinout, so anyone
with that board passes only `-DPICO_BOARD=<name>`. See [boards/](boards) for
the list and how to add one. Precedence is `-DJTAG_PINS` > board file >
`board_config.h`.
```

Pin numbers must have **no leading zeros** — `08` is an invalid octal literal.

`JTAG_PINS`, `PICO_PLATFORM` and `BOARD` are cached by CMake, so use a separate
build directory per configuration rather than reconfiguring in place. The
configure step always prints the resulting pinout; check it before flashing.

| Option | Effect |
|---|---|
| `-DJTAG_PINS="9;8;7;6"` | pinout, order TCK TDI TDO TMS |
| `-DRP2040=1` | build for RP2040 instead of RP2350B |
| `-DSINGLE_CHANNEL=1` | present as FT232H rather than FT2232H |
| `-DQUAD=1` | present as FT4232H with Xilinx EEPROM persona, for Vivado |
| `-DBUILD_TAG=xy` | shows in the USB serial as `FTxy....` |
| `-DPRODUCT_STRING="..."` | match a known-good cable's product string |
| `-DTDO_PULLDOWN=1` | bias TDO low, for bring-up |

## Vivado / Xilinx hw_server

Build with the QUAD option:

```sh
cmake -DQUAD=1 ..
make -j$(nproc)
```

Flash `pio_ftdi.uf2` as usual. The device enumerates as an FT4232H
(`0403:6011`) carrying the Xilinx EEPROM persona that `hw_server` requires.
The default persona is part of the firmware image: it needs no post-flash
step and survives reboots and re-flashes by construction. Vivado's Hardware
Manager lists the adapter as `Xilinx/PIOJTAG000A`.

### Custom serial (optional)

The only reason to touch the EEPROM is choosing your own serial. AMD's own
generator ships with every Vivado install; one invocation persists across
power cycles, with no further maintenance steps:

```sh
vivado -mode batch -source $VIVADO/scripts/program_ftdi/program_ftdi.tcl \
  -tclargs FT4232H <serial> pio-ftdi "PIO-JTAG-01 FT4232H" Xilinx \
  "pio-ftdi JTAG adapter"
```

The EEPROM image layout and checksum are documented in
[docs/eeprom-contract.md](docs/eeprom-contract.md); `tools/gen_eeprom_img.py`
regenerates the compiled default from spec and self-tests against a captured
AMD write.

### Known Linux issue

The `ftdi_sio` kernel driver binds interface 0 at enumeration and hides the
device from D2XX discovery. Workaround: run `hw_server` once — it claims
and releases the interface, after which Vivado finds the device normally.

## One tree, both parts

RP2040 and RP2350 differ in exactly one thing that matters here: RP2350B's PIO
sees a 32-GPIO window that must be selected with `pio_set_gpio_base()`, while
RP2040's reaches every GPIO directly and has no such API. That is a single
`#if` on the SDK's own `PICO_PIO_USE_GPIO_BASE`, plus deriving the window from
the pins so a pinout change cannot pick the wrong one.

Everything else is shared. The system clock is never set explicitly — TCK is
derived from `clk_sys` at runtime, so 125 MHz and 150 MHz both come out right
with no per-part constants. Pin range checks use `NUM_BANK0_GPIOS`.

## Wiring

Four signals plus a common ground. JTAG passes straight through — unlike a
UART, TDI does **not** cross to TDO.

| Adapter | Target FPGA |
|---|---|
| TCK | TCK |
| TDI | TDI |
| TDO | TDO |
| TMS | TMS |
| GND | GND |

**Ground is not optional.** Between two separately powered boards, four signal
wires with no common ground gives you a floating TDO that reads as all-ones —
which looks exactly like a dead target.

**Check the IO voltage.** The RP2040/RP2350 drive 3.3V. If the target's JTAG
bank runs at 1.8V, driving it directly is out of spec; use level shifters.

TDO is biased high internally, so an undriven line reads all-ones. That is also
the correct idle reading from a healthy target: a JTAG TAP tri-states TDO except
during Shift-DR/Shift-IR, so a high TDO on its own tells you nothing.

### Default pinouts

| Board file | TCK | TDI | TDO | TMS |
|---|---|---|---|---|
| `pico_jtag` (Pico, RP2040) | GPIO1 | GPIO2 | GPIO3 | GPIO4 |
| `pico2_jtag` (Pico 2, RP2350A) | GPIO1 | GPIO2 | GPIO3 | GPIO4 |
| `zen` (RP2350B + Trion T4) | GPIO30 | GPIO31 | GPIO32 | GPIO33 |
| none — `board_config.h` default | GPIO30 | GPIO31 | GPIO32 | GPIO33 |

These are GPIO numbers, not physical pin numbers. On a Pico, GPIO1-4 are
physical pins 2, 4, 5 and 6.

Any GPIO works. On RP2350B all four must sit within one 32-GPIO PIO window
(0-31 or 16-47); a `_Static_assert` fails the build otherwise.

## Bring-up

Work up the ladder in order. Starting at the FPGA vendor tool is how this
becomes a week of guessing.

```sh
lsusb -d 0403:6014          # enumerates as an FT232H
python3 test/rawprobe.py    # USB + MPSSE.  Want: 01 60 fa ab
python3 test/loopback.py    # jumper TDI to TDO
python3 test/scan.py        # real IDCODE off the target
```

`loopback.py` matters most: with TDI physically jumpered to TDO it proves the
PIO drives and reads the pins you think it does and that bit alignment is
correct, with no FPGA attached. Needs `pyusb`.

## How it works

**One state machine, not two.** Within a PIO block the higher-numbered SM wins
a contested output pin *even while disabled*, so separate data and TMS state
machines sharing TCK would fight. Instead one SM swaps `PINCTRL` between two
configurations — a single register write:

| Mode | OUT | SET | IN | SIDESET |
|------|-----|-----|----|---------|
| DATA | TDI | TMS | TDO | TCK |
| TMS  | TMS | TDI | TDO | TCK |

TDI must be held at a fixed level during TMS shifts (MPSSE puts it in bit 7 of
the command's data byte). That level is applied with `pio_sm_exec()` injecting
a `set pins` while the state machine is stalled at its leading `pull`.

### Three things that cost real time

**PIO's input synchroniser makes the obvious loop wrong.** GPIO inputs pass
through two flip-flops, so `in pins` reads the pin as it was 2 cycles ago. A
naive 4-cycle bit period lands that read on the falling edge, where the target
is retiming TDO, and you get plausible-looking but corrupt scan data. Sampling
merely *at* the rising edge is also too early for some targets — an Efinix
Trion has not updated TDO yet, which costs a whole bit and yields IDCODEs wrong
by exactly one shift. This uses 12 cycles per bit with the read landing
mid-high-phase, where a real FTDI samples.

**MPSSE `0x4B` is a write command.** Bit 6 (TMS) is itself the write flag, so
TMS commands legitimately have bits 4 and 5 clear. Reject commands with neither
bit set and every TAP state transition silently disappears — sync passes,
loopback passes, and the TAP never moves.

**Every bulk IN packet needs a 2-byte status prefix**, and the device must keep
emitting bare 2-byte packets on the latency-timer cadence when idle, or the
host's read blocks forever.

## Notes

**This clones FTDI's VID/PID.** Fine on your own bench; think hard before
shipping it in a product.

**Full-speed USB only**, so throughput is well below a real FT232H. Fine for
bitstreams; ILA captures feel slow.

**Single channel** — an FT232H, not an FT2232H.

**An FPGA already in user mode** will not take a new bitstream over JTAG unless
something can put it back into configuration mode. If your board cannot assert
`CRESET_N`, repeat programming needs a power cycle — with any adapter,
including a genuine FTDI.

## Licence

MIT.
