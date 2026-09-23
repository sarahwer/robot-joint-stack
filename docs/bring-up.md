# Bring-up log: two NUCLEO-H7S3L8 on CAN FD

A step-by-step checklist for the first hardware test, plus a place to record what happened.
Keep the log honest: problems found and fixed here are good interview material.

## Wiring

Each board needs an external CAN FD transceiver (the Nucleo has none), for example the
Adafruit CAN Pal (TJA1051T/3) or MikroE MCP2542 Click.

| NUCLEO-H7S3L8 | Transceiver | Note |
|---|---|---|
| PD1 (FDCAN1_TX) | TX / TXD | MCU TX → transceiver TXD |
| PD0 (FDCAN1_RX) | RX / RXD | transceiver RXD → MCU RX |
| 3V3 | VIO / 3V3 | logic level |
| 5V | VCC | the TJA1051 bus side needs 5 V; some modules generate it on board, check the module's pinout |
| GND | GND | common ground between both boards |

Bus: twisted pair CANH–CANH, CANL–CANL, **120 Ω at each end** (many modules have a solder jumper or
switch for this; enable it on both). Check with a multimeter, power off: CANH–CANL ≈ 60 Ω.

Find PD0/PD1 on the Arduino or ST morpho connector in the NUCLEO-H7S3L8 user manual (board MB1737) before wiring.

## Checklist

1. **Host side first**
   - [ ] `./scripts/fetch_deps.sh`, `ctest --preset host-tests`: all tests pass
   - [ ] both firmware presets build
2. **One board, no CAN**
   - [ ] flash node 1, UART shows the start banner
   - [ ] LD1 blinks at 5 Hz (comms task alive), LD2 looks steadily lit (500 Hz toggle)
   - [ ] logic analyzer on LD2 (PD13): 500 Hz square wave → control loop runs at 1 kHz
   - [ ] if the banner says `FATAL: oscillator config`: the HSE did not start, check the board's HSE
         source in the user manual (solder bridges / ST-LINK MCO)
3. **Two boards on the bus**
   - [ ] flash node 2, connect the bus
   - [ ] both logs show `[bus] node X joined`
   - [ ] `tec 0 rec 0` on both boards after one minute
   - [ ] logic analyzer / oscilloscope on CANH–CANL or the TX pin: arbitration at 1 Mbit/s (1 µs bits),
         data phase at 2 Mbit/s (500 ns bits)
   - [ ] node 2 position follows the ±90° sweep
4. **Fault tests**
   - [ ] press B1 on node 1: after ~500 ms node 2 stops (mode DISABLED, flag PEER_LOST)
   - [ ] unplug node 2: node 1 logs `node 2 LOST` within 350 ms
   - [ ] reset node 2: node 1 logs `node 2 joined` again (or `rebooted` if the reset was very short)
   - [ ] remove one terminator / short CANH–CANL briefly: TEC rises, error LED on, bus recovers afterwards

## Log

| Date | Step | Result | Notes / fix |
|---|---|---|---|
| | | | |
