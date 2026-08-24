# Virtual EEPROM — storage contract (pio-ftdi QUAD mode)

The FT4232H persona needs an EEPROM image: Xilinx `hw_server` reads it at
enumeration and rejects devices whose image it does not accept. The image
below was derived from AMD's own `program_ftdi` (shipped in every Vivado
install, `scripts/program_ftdi/`) and verified word-for-word against a
captured AMD write (see the self-test in `tools/gen_eeprom_img.py`).

## Layout (128 words)

| Words | Content |
|---|---|
| 0x00-0x0B | FTDI config words (VID 0x0403, PID 0x6011, flags) |
| 0x0C | header word `0x0056` |
| 0x0D-0x0E | Xilinx signature `0x0004 0x584A` |
| 0x0F-0x1D | user area: `vendor"\0"board"\0"` UTF-8, packed 2 bytes/word, low byte first |
| 0x4D-0x75 | USB string blocks, chained: marker `(0x03<<8)\|(len)` then one word per UTF-16LE char — manufacturer, product, serial, empty terminator `0x0302` |
| 0x7F | checksum word (below) |

`hw_server` validates the checksum: an otherwise valid image with a wrong
0x7F word is rejected (verified experimentally).

## Checksum

```
cs = 0xAAAA
for word in 0x00..0x7E:  cs = rol16(cs ^ word, 1)   # rotate left 1
word[0x7F] = cs
```

Source: intra2net libftdi, `src/ftdi.c` (`ftdi_eeprom_build`). Verified
against two independently captured images (a vendor cable and an AMD
`program_ftdi` write); both match.

## Persistence

- The compiled default image is part of the firmware; every boot starts
  from it unless a valid record exists.
- Any EEPROM write (SIO 0x91/0x92) marks the table dirty; after a 2 s
  debounce with no further writes, the firmware stores the table in the
  last 4 KB flash sector: header (magic `PI0F`/`EEPR`), 128 words, 16-bit
  sum. On boot, a valid record replaces the compiled default.
- Flash erase suspends interrupts long enough to wedge the USB stack, so
  the commit detaches from the bus (`tud_disconnect`), erases and
  programs the sector, then re-attaches (`tud_connect`). The host sees a
  normal detach/attach; the RAM table and persona are unaffected.
- The record survives power cycles and firmware re-flashing (the UF2 does
  not touch the last sector). There is no factory-reset request in the
  shipped build; programming the default serial again is the supported
  way back.

## Limitations

- Words 0x08-0x09 are copied verbatim from the AMD write for the default
  string lengths. Personas with different string lengths may require
  adjusted values (not decoded).
- The table is 128 words and SIO addresses wrap mod 128, matching the
  93C56 window the host tools read. AMD writes a 256-word space; the
  high half wraps into the table. Observed stable; documented as-is.

## Sources

- AMD UG908 (FTDI EEPROM programming for Vivado)
- AMD `scripts/program_ftdi/` (shipped with Vivado) — layout, write format
- intra2net libftdi `src/ftdi.c` — checksum algorithm
