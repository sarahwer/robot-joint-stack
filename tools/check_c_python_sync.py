#!/usr/bin/env python3
"""
Reads frames produced by tests/dump_frames.c (stdin) and checks that the
Python decoder recovers exactly the values the C side encoded.

SPDX-License-Identifier: MIT
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

import rjs_protocol as p  # noqa: E402

EXPECTED = [
    ("HEARTBEAT", 2, {"state": "OPERATIONAL", "errors": "PEER_LOST", "uptime_s": 123.456, "fw": "0.1.0"}),
    ("COMMAND", 0, {"to": "ALL", "mode": "POSITION", "target_deg": -90.0, "vmax_deg_s": 90.0, "seq": 77}),
    ("JOINT_STATE", 2, {"mode": "POSITION", "pos_deg": 45.5, "vel_deg_s": -30.0, "current_ma": 80,
                        "temp_c": 25.1, "seq": 77}),
    ("EMERGENCY", 3, {"code": "0x1001", "detail": "0x0000002a"}),
]


def main() -> int:
    frames = [p.parse_candump_line(line) for line in sys.stdin if line.strip()]
    if len(frames) != len(EXPECTED):
        print(f"expected {len(EXPECTED)} frames, got {len(frames)}")
        return 1
    ok = True
    for (ts, can_id, data), (kind, node, fields) in zip(frames, EXPECTED):
        d = p.decode(can_id, data)
        if d is None or d.kind != kind or d.node != node or d.fields != fields:
            print(f"MISMATCH\n  expected {kind} node {node} {fields}\n  got      {d}")
            ok = False
        else:
            print(f"ok {d}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
