"""
Tests for tools/rjs_protocol.py. The byte strings below are the exact frames
the C encoder produces (see tests/test_protocol.c), so C and Python stay in sync.

Run:  python3 -m pytest tools/   (or: python3 tools/test_rjs_protocol.py)

SPDX-License-Identifier: MIT
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

import rjs_protocol as p  # noqa: E402


def test_id_layout_matches_c():
    assert p.make_id(p.MSG_HEARTBEAT, 5) == 0x385
    assert p.split_id(0x385) == (p.MSG_HEARTBEAT, 5)


def test_heartbeat():
    # state OPERATIONAL, flags 0x0102, uptime 0xA1B2C3D4, fw 0.1.2
    data = bytes([1, 2, 0x02, 0x01, 0xD4, 0xC3, 0xB2, 0xA1, 0, 1, 2, 0])
    d = p.decode(p.make_id(p.MSG_HEARTBEAT, 3), data)
    assert d.node == 3
    assert d.fields["state"] == "OPERATIONAL"
    assert d.fields["errors"] == "CAN_BUS_OFF"   # 0x0102: bit 1 set, bit 8 is unassigned
    assert d.fields["uptime_s"] == round(0xA1B2C3D4 / 1000.0, 3)
    assert d.fields["fw"] == "0.1.2"


def test_command_negative_target():
    import struct
    data = bytes([1, 1]) + struct.pack("<iiH", -90000, 45000, 65535)
    d = p.decode(p.make_id(p.MSG_COMMAND, 0), data)
    assert d.fields["to"] == "ALL"
    assert d.fields["target_deg"] == -90.0
    assert d.fields["seq"] == 65535


def test_joint_state():
    import struct
    data = bytes([1, 1]) + struct.pack("<iihhH", -170000, 123456, -250, -55, 42)
    d = p.decode(p.make_id(p.MSG_JOINT_STATE, 9), data)
    assert d.fields["pos_deg"] == -170.0
    assert d.fields["temp_c"] == -5.5
    assert d.fields["current_ma"] == -250


def test_bad_version_rejected():
    try:
        p.decode(p.make_id(p.MSG_HEARTBEAT, 1), bytes([99] + [0] * 11))
    except ValueError:
        return
    raise AssertionError("expected ValueError")


def test_candump_fd_line():
    line = "(1695040000.123456) can0 385##1010200000000000000010200"
    ts, can_id, data = p.parse_candump_line(line)
    assert can_id == 0x385
    assert data[0] == 1


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
