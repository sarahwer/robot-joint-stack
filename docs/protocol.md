# CAN FD protocol (v1)

Implemented in [`common/protocol`](../common/protocol) (C) and [`tools/rjs_protocol.py`](../tools/rjs_protocol.py)
(Python). A CTest case encodes frames in C and decodes them in Python, so both stay in sync.

## Bus parameters

| Parameter | Value |
|---|---|
| Frame format | CAN FD with bit rate switching (BRS), 11-bit identifiers |
| Nominal (arbitration) bit rate | 1 Mbit/s, sample point 80 % |
| Data bit rate | 2 Mbit/s, sample point 80 % |
| Termination | 120 Ω at both ends of the bus |

## Identifier layout

```
 bit 10    7 6            0
    ┌──────┬──────────────┐
    │ type │   node ID    │
    └──────┴──────────────┘
     4 bit      7 bit (1..127, 0 = broadcast)
```

A lower identifier wins arbitration, so the type field sets the priority for the whole bus:
an EMERGENCY from node 127 still beats a COMMAND from node 1.

| Type | Value | ID range | Direction | Rate |
|---|---|---|---|---|
| EMERGENCY | 0x0 | 0x000–0x07F | any node → all | on event |
| COMMAND | 0x1 | 0x080–0x0FF | controller → node (0 = all) | 10 Hz |
| JOINT_STATE | 0x2 | 0x100–0x17F | node → all | 100 Hz |
| SENSOR | 0x3 | 0x180–0x1FF | node → all | reserved |
| HEARTBEAT | 0x7 | 0x380–0x3FF | node → all | 10 Hz |

## Payloads

All fields are little-endian and encoded byte by byte (no struct casting), so the format does not depend
on compiler, CPU or endianness. Byte 0 is always the protocol version (1). Payload sizes are valid
CAN FD lengths.

**HEARTBEAT, 12 bytes**

| Byte | Field | Type | Note |
|---|---|---|---|
| 0 | version | u8 | 1 |
| 1 | state | u8 | 0 BOOT, 1 PREOP, 2 OPERATIONAL, 3 FAULT |
| 2–3 | error_flags | u16 | see below |
| 4–7 | uptime_ms | u32 | a decrease means the node rebooted |
| 8–10 | fw major/minor/patch | u8 ×3 | |
| 11 | reserved | u8 | 0 |

**COMMAND, 12 bytes**

| Byte | Field | Type | Note |
|---|---|---|---|
| 0 | version | u8 | |
| 1 | mode | u8 | 0 DISABLED, 1 POSITION |
| 2–5 | target | i32 | millidegrees, clamped to the joint's soft limits |
| 6–9 | max_velocity | i32 | millidegrees/s, must be > 0 in POSITION mode (checked by the decoder) |
| 10–11 | sequence | u16 | echoed in JOINT_STATE |

**JOINT_STATE, 16 bytes**

| Byte | Field | Type | Note |
|---|---|---|---|
| 0 | version | u8 | |
| 1 | mode | u8 | |
| 2–5 | position | i32 | millidegrees |
| 6–9 | velocity | i32 | millidegrees/s |
| 10–11 | current | i16 | mA (model value until a motor is connected) |
| 12–13 | temperature | i16 | 0.1 °C |
| 14–15 | last_command_seq | u16 | |

**EMERGENCY, 8 bytes**: 0 version · 1 reserved · 2–3 code (u16) · 4–7 detail (u32)

## Error flags

| Bit | Name | Set when |
|---|---|---|
| 0x0001 | CAN_PASSIVE | CAN controller is error passive |
| 0x0002 | CAN_BUS_OFF | CAN controller is bus-off (recovering) |
| 0x0004 | TX_DROPPED | at least one frame was dropped because the TX FIFO was full |
| 0x0008 | PEER_LOST | the joint stopped because commands timed out |
| 0x0010 | OVER_TEMP | reserved |
| 0x0020 | LIMIT_REACHED | last target was outside the soft limits and was clamped |

## Timing rules

| Rule | Value | Where |
|---|---|---|
| Peer considered LOST | no heartbeat for 350 ms | `app.c` (`PEER_TIMEOUT_MS`) |
| Joint disables itself | no command for 500 ms | `app.c` (`command_timeout_ms`) |
| Bus load at 2 nodes | about 250 frames/s | 2 × (100 + 10) + 10 |
