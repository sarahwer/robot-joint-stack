#!/usr/bin/env python3
"""
rjs_protocol.py - Python mirror of common/protocol (wire format v1).

Used on the PC or the Raspberry Pi 5 to decode bus traffic, e.g. from a
SocketCAN interface:

    candump -L can0 | python3 tools/rjs_protocol.py

Each line of `candump -L` output looks like:
    (1695040000.123456) can0 385##10200000000...     (CAN FD, flags nibble after '##')
    (1695040000.123456) can0 385#1122334455667788    (classic CAN)

SPDX-License-Identifier: MIT
"""
from __future__ import annotations

import re
import struct
import sys
from dataclasses import dataclass
from typing import Optional

PROTOCOL_VERSION = 1

MSG_EMERGENCY = 0x0
MSG_COMMAND = 0x1
MSG_JOINT_STATE = 0x2
MSG_SENSOR = 0x3
MSG_HEARTBEAT = 0x7

NODE_STATES = {0: "BOOT", 1: "PREOP", 2: "OPERATIONAL", 3: "FAULT"}
MODES = {0: "DISABLED", 1: "POSITION"}
ERROR_FLAGS = {
    0x0001: "CAN_PASSIVE",
    0x0002: "CAN_BUS_OFF",
    0x0004: "TX_DROPPED",
    0x0008: "PEER_LOST",
    0x0010: "OVER_TEMP",
    0x0020: "LIMIT_REACHED",
}


def make_id(msg_type: int, node: int) -> int:
    return ((msg_type & 0x0F) << 7) | (node & 0x7F)


def split_id(can_id: int) -> tuple[int, int]:
    return (can_id >> 7) & 0x0F, can_id & 0x7F


def flags_text(flags: int) -> str:
    names = [name for bit, name in ERROR_FLAGS.items() if flags & bit]
    return "|".join(names) if names else "-"


@dataclass
class Decoded:
    node: int
    kind: str
    fields: dict

    def __str__(self) -> str:
        body = " ".join(f"{k}={v}" for k, v in self.fields.items())
        return f"node {self.node:3d} {self.kind:<11} {body}"


def decode(can_id: int, data: bytes) -> Optional[Decoded]:
    """Decode one frame. Returns None for unknown types, raises ValueError for bad frames."""
    msg_type, node = split_id(can_id)
    if len(data) < 1 or data[0] != PROTOCOL_VERSION:
        raise ValueError(f"id 0x{can_id:03x}: bad or missing protocol version")

    if msg_type == MSG_HEARTBEAT:
        if len(data) < 12:
            raise ValueError("heartbeat too short")
        state, flags, uptime, major, minor, patch = struct.unpack_from("<BHIBBB", data, 1)
        return Decoded(node, "HEARTBEAT", {
            "state": NODE_STATES.get(state, state),
            "errors": flags_text(flags),
            "uptime_s": round(uptime / 1000.0, 3),
            "fw": f"{major}.{minor}.{patch}",
        })
    if msg_type == MSG_COMMAND:
        if len(data) < 12:
            raise ValueError("command too short")
        mode, target, vmax, seq = struct.unpack_from("<BiiH", data, 1)
        return Decoded(node, "COMMAND", {
            "to": "ALL" if node == 0 else node,
            "mode": MODES.get(mode, mode),
            "target_deg": target / 1000.0,
            "vmax_deg_s": vmax / 1000.0,
            "seq": seq,
        })
    if msg_type == MSG_JOINT_STATE:
        if len(data) < 16:
            raise ValueError("joint state too short")
        mode, pos, vel, cur, temp, seq = struct.unpack_from("<BiihhH", data, 1)
        return Decoded(node, "JOINT_STATE", {
            "mode": MODES.get(mode, mode),
            "pos_deg": pos / 1000.0,
            "vel_deg_s": vel / 1000.0,
            "current_ma": cur,
            "temp_c": temp / 10.0,
            "seq": seq,
        })
    if msg_type == MSG_EMERGENCY:
        if len(data) < 8:
            raise ValueError("emergency too short")
        code, detail = struct.unpack_from("<HI", data, 2)
        return Decoded(node, "EMERGENCY", {"code": f"0x{code:04x}", "detail": f"0x{detail:08x}"})
    return None


# ---- candump -L parsing ----------------------------------------------------

_CANDUMP = re.compile(r"^\((?P<ts>[\d.]+)\)\s+(?P<ifc>\S+)\s+(?P<id>[0-9A-Fa-f]{3,8})#(?P<fd>#[0-9A-Fa-f])?(?P<data>[0-9A-Fa-f]*)")


def parse_candump_line(line: str) -> Optional[tuple[float, int, bytes]]:
    m = _CANDUMP.match(line.strip())
    if not m:
        return None
    return float(m["ts"]), int(m["id"], 16), bytes.fromhex(m["data"])


def main() -> int:
    for line in sys.stdin:
        parsed = parse_candump_line(line)
        if parsed is None:
            continue
        ts, can_id, data = parsed
        try:
            d = decode(can_id, data)
        except ValueError as e:
            print(f"{ts:.6f} ! {e}")
            continue
        if d is not None:
            print(f"{ts:.6f} {d}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
