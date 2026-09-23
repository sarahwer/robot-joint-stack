# Measurements

Numbers measured on real hardware. Fill in the table as you go and add the screenshots to
`docs/img/`. Measured numbers (with the method) are what makes a portfolio project credible.

## Setup

- 2 × NUCLEO-H7S3L8, firmware version: `…`, commit: `…`
- Transceivers: `…`, cable length: `…`, termination: 2 × 120 Ω
- Instruments: logic analyzer `…`, oscilloscope `…`

## Results

| What | How | Result |
|---|---|---|
| Control loop period / jitter | logic analyzer on LD2 (PD13): each edge = one loop; also printed as `jitter` in the stats line (DWT cycle counter) | |
| CPU load of the control loop | toggle a pin at start and end of the loop body, measure the high time | |
| CAN bit timing | scope on CANH–CANL: 1 µs arbitration bits, 500 ns data bits | |
| Bus load | frames/s × frame length (logic analyzer CAN decoder) | |
| Command → state latency | node 1 TX of COMMAND to node 2 JOINT_STATE with the new sequence number | |
| Node loss detection time | unplug node 2, time until `LOST` | expected ≤ 350 ms |
| Joint stop after command loss | press B1 on node 1, time until node 2 velocity = 0 | expected ≈ 500 ms + braking |
| Bus-off recovery | short CANH–CANL, release, time until frames flow again | |
| Flash / RAM usage | `arm-none-eabi-size` | 35 KB flash / 50 KB RAM (build of <date>) |

## Screenshots

<!-- ![Control loop on LD2](img/loop_timing.png) -->
<!-- ![CAN FD frame with BRS](img/canfd_frame.png) -->
