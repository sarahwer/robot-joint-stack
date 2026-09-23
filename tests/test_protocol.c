/*
 * test_protocol.c - round trips, wire format and error handling.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "rjs/protocol.h"
#include "test_framework.h"

static void test_id_layout(void)
{
    CHECK_EQ(rjs_make_id(RJS_MSG_HEARTBEAT, 5), 0x385);
    CHECK_EQ(rjs_make_id(RJS_MSG_EMERGENCY, 1), 0x001);
    CHECK_EQ(rjs_id_type(0x385), RJS_MSG_HEARTBEAT);
    CHECK_EQ(rjs_id_node(0x385), 5);
    /* Emergency must win arbitration against any other message of any node. */
    CHECK(rjs_make_id(RJS_MSG_EMERGENCY, 127) < rjs_make_id(RJS_MSG_COMMAND, 1));
    CHECK(rjs_make_id(RJS_MSG_COMMAND, 127) < rjs_make_id(RJS_MSG_JOINT_STATE, 1));
}

static void test_fd_lengths(void)
{
    CHECK_EQ(rjs_fd_len_to_dlc(8), 8);
    CHECK_EQ(rjs_fd_len_to_dlc(12), 9);
    CHECK_EQ(rjs_fd_len_to_dlc(64), 15);
    CHECK_EQ(rjs_fd_len_to_dlc(10), 0xFF);
    CHECK_EQ(rjs_fd_dlc_to_len(13), 32);
    CHECK_EQ(rjs_fd_dlc_to_len(16), 0xFF);
    CHECK_EQ(rjs_fd_round_len(9), 12);
    CHECK_EQ(rjs_fd_round_len(33), 48);
    CHECK_EQ(rjs_fd_round_len(65), 0);
    for (uint8_t dlc = 0; dlc < 16; ++dlc) {
        CHECK_EQ(rjs_fd_len_to_dlc(rjs_fd_dlc_to_len(dlc)), dlc);
    }
}

static void test_heartbeat_roundtrip_and_wire_format(void)
{
    rjs_heartbeat_t hb = {.state = RJS_NODE_STATE_OPERATIONAL, .error_flags = 0x0102,
                          .uptime_ms = 0xA1B2C3D4u, .fw_major = 0, .fw_minor = 1, .fw_patch = 2};
    rjs_can_frame_t f;
    CHECK_EQ(rjs_encode_heartbeat(3, &hb, &f), RJS_PROTO_OK);
    CHECK_EQ(f.id, rjs_make_id(RJS_MSG_HEARTBEAT, 3));
    CHECK_EQ(f.len, 12);
    CHECK(f.fd && f.brs);
    /* Little-endian on the wire, independent of the CPU. */
    CHECK_EQ(f.data[0], RJS_PROTOCOL_VERSION);
    CHECK_EQ(f.data[2], 0x02);
    CHECK_EQ(f.data[3], 0x01);
    CHECK_EQ(f.data[4], 0xD4);
    CHECK_EQ(f.data[7], 0xA1);

    rjs_heartbeat_t out;
    memset(&out, 0xEE, sizeof(out));
    CHECK_EQ(rjs_decode_heartbeat(&f, &out), RJS_PROTO_OK);
    CHECK_EQ(out.state, hb.state);
    CHECK_EQ(out.error_flags, hb.error_flags);
    CHECK_EQ(out.uptime_ms, hb.uptime_ms);
    CHECK_EQ(out.fw_minor, 1);
    CHECK_EQ(out.fw_patch, 2);
}

static void test_command_negative_values(void)
{
    rjs_command_t c = {.mode = RJS_MODE_POSITION, .target_mdeg = -90000,
                       .max_velocity_mdeg_s = 45000, .sequence = 65535};
    rjs_can_frame_t f;
    CHECK_EQ(rjs_encode_command(2, &c, &f), RJS_PROTO_OK);
    rjs_command_t out;
    CHECK_EQ(rjs_decode_command(&f, &out), RJS_PROTO_OK);
    CHECK_EQ(out.target_mdeg, -90000);
    CHECK_EQ(out.max_velocity_mdeg_s, 45000);
    CHECK_EQ(out.sequence, 65535);
}

static void test_joint_state_roundtrip(void)
{
    rjs_joint_state_t s = {.position_mdeg = -170000, .velocity_mdeg_s = 123456, .current_ma = -250,
                           .temperature_c10 = -55, .last_command_seq = 42, .mode = RJS_MODE_POSITION};
    rjs_can_frame_t f;
    CHECK_EQ(rjs_encode_joint_state(9, &s, &f), RJS_PROTO_OK);
    CHECK_EQ(f.len, 16);
    rjs_joint_state_t out;
    CHECK_EQ(rjs_decode_joint_state(&f, &out), RJS_PROTO_OK);
    CHECK_EQ(out.position_mdeg, -170000);
    CHECK_EQ(out.velocity_mdeg_s, 123456);
    CHECK_EQ(out.current_ma, -250);
    CHECK_EQ(out.temperature_c10, -55);
    CHECK_EQ(out.last_command_seq, 42);
}

static void test_emergency_roundtrip(void)
{
    rjs_emergency_t e = {.code = 0x1001, .detail = 0xDEADBEEF};
    rjs_can_frame_t f;
    CHECK_EQ(rjs_encode_emergency(1, &e, &f), RJS_PROTO_OK);
    rjs_emergency_t out;
    CHECK_EQ(rjs_decode_emergency(&f, &out), RJS_PROTO_OK);
    CHECK_EQ(out.code, 0x1001);
    CHECK_EQ(out.detail, 0xDEADBEEF);
}

static void test_decode_rejects_bad_frames(void)
{
    rjs_heartbeat_t hb = {.state = RJS_NODE_STATE_PREOP};
    rjs_can_frame_t f;
    rjs_heartbeat_t out;
    rjs_joint_state_t js;
    CHECK_EQ(rjs_encode_heartbeat(1, &hb, &f), RJS_PROTO_OK);

    rjs_can_frame_t short_frame = f;
    short_frame.len = 8;
    CHECK_EQ(rjs_decode_heartbeat(&short_frame, &out), RJS_PROTO_ERR_LENGTH);

    rjs_can_frame_t bad_version = f;
    bad_version.data[0] = 99;
    CHECK_EQ(rjs_decode_heartbeat(&bad_version, &out), RJS_PROTO_ERR_VERSION);

    CHECK_EQ(rjs_decode_joint_state(&f, &js), RJS_PROTO_ERR_TYPE);
    CHECK_EQ(rjs_decode_heartbeat(NULL, &out), RJS_PROTO_ERR_PARAM);
}

static void test_encode_rejects_invalid_input(void)
{
    rjs_heartbeat_t hb = {.state = RJS_NODE_STATE_PREOP};
    rjs_can_frame_t f;
    CHECK_EQ(rjs_encode_heartbeat(0, &hb, &f), RJS_PROTO_ERR_PARAM);   /* broadcast is not a sender */
    CHECK_EQ(rjs_encode_heartbeat(128, &hb, &f), RJS_PROTO_ERR_PARAM);
    hb.state = 7;
    CHECK_EQ(rjs_encode_heartbeat(1, &hb, &f), RJS_PROTO_ERR_PARAM);

    rjs_command_t c = {.mode = RJS_MODE_POSITION, .target_mdeg = 0, .max_velocity_mdeg_s = 0};
    CHECK_EQ(rjs_encode_command(1, &c, &f), RJS_PROTO_ERR_PARAM);    /* zero velocity limit */
    c.max_velocity_mdeg_s = 1000;
    CHECK_EQ(rjs_encode_command(RJS_NODE_BROADCAST, &c, &f), RJS_PROTO_OK);
}

static void test_decode_rejects_unsafe_command(void)
{
    rjs_command_t c = {.mode = RJS_MODE_POSITION, .target_mdeg = 1000, .max_velocity_mdeg_s = 1000};
    rjs_can_frame_t f;
    CHECK_EQ(rjs_encode_command(1, &c, &f), RJS_PROTO_OK);
    /* Corrupt the velocity limit to a negative value on the wire. */
    f.data[9] = 0x80;
    rjs_command_t out = {0};
    CHECK_EQ(rjs_decode_command(&f, &out), RJS_PROTO_ERR_PARAM);
    CHECK_EQ(out.target_mdeg, 0); /* output untouched on error */
}

int main(void)
{
    RUN(test_id_layout);
    RUN(test_fd_lengths);
    RUN(test_heartbeat_roundtrip_and_wire_format);
    RUN(test_command_negative_values);
    RUN(test_joint_state_roundtrip);
    RUN(test_emergency_roundtrip);
    RUN(test_decode_rejects_bad_frames);
    RUN(test_encode_rejects_invalid_input);
    RUN(test_decode_rejects_unsafe_command);
    TEST_MAIN_END();
}
