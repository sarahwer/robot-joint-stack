# Status display: 2.0" ST7789 TFT (optional)

A 240x320 SPI TFT ("GMT020-02-7P" / "K23-2.0-TFT", driver IC ST7789V) shows the bus and joint state
on node 1. It is off by default; build it with the `h7s3l8-node1-display` preset.

```bash
cmake --preset h7s3l8-node1-display && cmake --build --preset h7s3l8-node1-display
```

## Wiring

| Module (7 pins) | NUCLEO-H7S3L8 | Note |
|---|---|---|
| SCL | PA5 (Arduino D13) | SPI1_SCK |
| SDA | PB5 (Arduino D11) | SPI1_MOSI (this module has no MISO) |
| CS | PD14 | chip select |
| DC | PD15 | 0 = command, 1 = data |
| RST | PD11 | hardware reset |
| VCC | 3V3 | module regulator and backlight |
| GND | GND | |

All pins are defined at the top of
[`boards/nucleo_h7s3l8/src/display_st7789.c`](../boards/nucleo_h7s3l8/src/display_st7789.c).
Check the header positions of PD11/PD14/PD15 in the board user manual before wiring; none of them
collide with FDCAN (PD0/PD1), USART3 (PD8/PD9) or the LEDs (PD10/PD13).

## How it is built

- **240x320 with an 8x16 font = exactly 30 x 20 characters.** The application writes into a character
  screen model ([`common/ui/text_screen.c`](../common/ui/src/text_screen.c)) instead of a 150 KB
  framebuffer, which would not be a good use of the 320 KB of RAM.
- **Only changed characters are redrawn.** A typical update touches a handful of cells, so the SPI
  traffic per update is a few hundred bytes instead of 150 KB.
- **SPI with DMA:** each cell (8x16 pixels = 256 bytes) goes out by DMA; the task waits on a semaphore
  that the completion interrupt gives. The data cache is cleaned before every transfer, because the
  DMA reads the buffer from AXI SRAM.
- **Priority:** the UI task runs below the control loop and the CAN receive task, so drawing can never
  delay the 1 kHz loop.
- The screen model and the font are chip-independent and unit tested on the PC
  ([`tests/test_text_screen.c`](../tests/test_text_screen.c)).

## Screen layout

```
ROBOT JOINT STACK    node 1
CAN FD 1/2 Mbit   up 123s

POSITION -45.3 deg
############............
vel -90 deg/s  mode POS

NODES ON BUS (1 alive)
 node 2   OPER
...
BUS
 tx 1234  rx 1180
 tec 0  rec 0  drop 0
 loop jitter <n> us

 RUNNING
```

The bottom line turns yellow on a command timeout or when the sweep is stopped with button B1, and
red when the CAN controller goes bus-off.

## Bring-up checklist

- [ ] Wire the module, power the board: the backlight comes on (the module has no backlight pin).
- [ ] After flashing: the screen clears to black and text appears within a second.
- [ ] Nothing visible? Check DC and RST first, then measure SCL with the logic analyzer
      (should be ~10 MHz bursts), and check that CS stays low for a whole cell transfer.
- [ ] Colours inverted or washed out: remove the `INVON` (0x21) command in `board_display_init()`;
      some panels of this type need it, others do not.
- [ ] Mirrored or rotated image: change the `MADCTL` (0x36) value.
- [ ] Measure the update cost: toggle a pin around `rjs_ts_flush()` and compare with the 200 ms period.

## Regenerating the font

```bash
python3 tools/gen_font.py            # writes common/ui/src/font8x16.c
```

The generated file is committed, so a normal build needs neither Python nor font files.
