/*
 * protocol.c - encode/decode for the robot-joint-stack CAN FD protocol.
 *
 * Payload layouts (byte offsets, little-endian):
 *
 *   HEARTBEAT   (12 B): 0 ver | 1 state | 2-3 error_flags | 4-7 uptime_ms |
 *                       8 fw_major | 9 fw_minor | 10 fw_patch | 11 reserved
 *   COMMAND     (12 B): 0 ver | 1 mode | 2-5 target_mdeg | 6-9 max_velocity |
 *                       10-11 sequence
 *   JOINT_STATE (16 B): 0 ver | 1 mode | 2-5 position | 6-9 velocity |
 *                       10-11 current_ma | 12-13 temperature_c10 | 14-15 last_seq
 *   EMERGENCY   ( 8 B): 0 ver | 1 reserved | 2-3 code | 4-7 detail
 *
 * SPDX-License-Identifier: MIT
 */
#include "rjs/protocol.h"

#include <string.h>

#define HEARTBEAT_LEN   12u
#define COMMAND_LEN     12u
#define JOINT_STATE_LEN 16u
#define EMERGENCY_LEN    8u

/* ---- little-endian helpers ---------------------------------------------- */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- CAN FD length table ---------------------------------------------------- */

static const uint8_t k_dlc_to_len[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

uint8_t rjs_fd_dlc_to_len(uint8_t dlc)
{
    return (dlc < 16u) ? k_dlc_to_len[dlc] : 0xFFu;
}

uint8_t rjs_fd_len_to_dlc(uint8_t len)
{
    for (uint8_t dlc = 0; dlc < 16u; ++dlc) {
        if (k_dlc_to_len[dlc] == len) {
            return dlc;
        }
    }
    return 0xFFu;
}

uint8_t rjs_fd_round_len(uint8_t len)
{
    for (uint8_t dlc = 0; dlc < 16u; ++dlc) {
        if (k_dlc_to_len[dlc] >= len) {
            return k_dlc_to_len[dlc];
        }
    }
    return 0u;
}

/* ---- common frame handling ------------------------------------------------ */

static void frame_start(rjs_can_frame_t *out, rjs_msg_type_t type, uint8_t node, uint8_t len)
{
    memset(out, 0, sizeof(*out));
    out->id  = rjs_make_id(type, node);
    out->len = len;
    out->fd  = true;
    out->brs = true;
    out->data[0] = RJS_PROTOCOL_VERSION;
}

static rjs_proto_result_t frame_check(const rjs_can_frame_t *in, rjs_msg_type_t type, uint8_t min_len)
{
    if (in == NULL) {
        return RJS_PROTO_ERR_PARAM;
    }
    if (rjs_id_type(in->id) != type) {
        return RJS_PROTO_ERR_TYPE;
    }
    if (in->len < min_len || in->len > RJS_CAN_MAX_PAYLOAD) {
        return RJS_PROTO_ERR_LENGTH;
    }
    if (in->data[0] != RJS_PROTOCOL_VERSION) {
        return RJS_PROTO_ERR_VERSION;
    }
    return RJS_PROTO_OK;
}

static bool node_valid(uint8_t node)
{
    return node >= 1u && node <= RJS_NODE_ID_MAX;
}

/* ---- HEARTBEAT ---------------------------------------------------------- */

rjs_proto_result_t rjs_encode_heartbeat(uint8_t node, const rjs_heartbeat_t *in, rjs_can_frame_t *out)
{
    if (in == NULL || out == NULL || !node_valid(node) || in->state > RJS_NODE_STATE_FAULT) {
        return RJS_PROTO_ERR_PARAM;
    }
    frame_start(out, RJS_MSG_HEARTBEAT, node, HEARTBEAT_LEN);
    out->data[1] = in->state;
    put_u16(&out->data[2], in->error_flags);
    put_u32(&out->data[4], in->uptime_ms);
    out->data[8]  = in->fw_major;
    out->data[9]  = in->fw_minor;
    out->data[10] = in->fw_patch;
    return RJS_PROTO_OK;
}

rjs_proto_result_t rjs_decode_heartbeat(const rjs_can_frame_t *in, rjs_heartbeat_t *out)
{
    rjs_proto_result_t r = frame_check(in, RJS_MSG_HEARTBEAT, HEARTBEAT_LEN);
    if (r != RJS_PROTO_OK) {
        return r;
    }
    if (out == NULL) {
        return RJS_PROTO_ERR_PARAM;
    }
    out->state       = in->data[1];
    out->error_flags = get_u16(&in->data[2]);
    out->uptime_ms   = get_u32(&in->data[4]);
    out->fw_major    = in->data[8];
    out->fw_minor    = in->data[9];
    out->fw_patch    = in->data[10];
    return RJS_PROTO_OK;
}

/* ---- COMMAND ---------------------------------------------------------- */

rjs_proto_result_t rjs_encode_command(uint8_t target_node, const rjs_command_t *in, rjs_can_frame_t *out)
{
    if (in == NULL || out == NULL || target_node > RJS_NODE_ID_MAX || in->mode > RJS_MODE_POSITION) {
        return RJS_PROTO_ERR_PARAM;
    }
    if (in->mode == RJS_MODE_POSITION && in->max_velocity_mdeg_s <= 0) {
        return RJS_PROTO_ERR_PARAM;
    }
    frame_start(out, RJS_MSG_COMMAND, target_node, COMMAND_LEN);
    out->data[1] = in->mode;
    put_u32(&out->data[2], (uint32_t)in->target_mdeg);
    put_u32(&out->data[6], (uint32_t)in->max_velocity_mdeg_s);
    put_u16(&out->data[10], in->sequence);
    return RJS_PROTO_OK;
}

rjs_proto_result_t rjs_decode_command(const rjs_can_frame_t *in, rjs_command_t *out)
{
    rjs_proto_result_t r = frame_check(in, RJS_MSG_COMMAND, COMMAND_LEN);
    if (r != RJS_PROTO_OK) {
        return r;
    }
    if (out == NULL) {
        return RJS_PROTO_ERR_PARAM;
    }
    rjs_command_t tmp;
    tmp.mode                = in->data[1];
    tmp.target_mdeg         = (int32_t)get_u32(&in->data[2]);
    tmp.max_velocity_mdeg_s = (int32_t)get_u32(&in->data[6]);
    tmp.sequence            = get_u16(&in->data[10]);
    /* Reject commands a joint must never execute. */
    if (tmp.mode > RJS_MODE_POSITION || (tmp.mode == RJS_MODE_POSITION && tmp.max_velocity_mdeg_s <= 0)) {
        return RJS_PROTO_ERR_PARAM;
    }
    *out = tmp;
    return RJS_PROTO_OK;
}

/* ---- JOINT_STATE ---------------------------------------------------------- */

rjs_proto_result_t rjs_encode_joint_state(uint8_t node, const rjs_joint_state_t *in, rjs_can_frame_t *out)
{
    if (in == NULL || out == NULL || !node_valid(node)) {
        return RJS_PROTO_ERR_PARAM;
    }
    frame_start(out, RJS_MSG_JOINT_STATE, node, JOINT_STATE_LEN);
    out->data[1] = in->mode;
    put_u32(&out->data[2], (uint32_t)in->position_mdeg);
    put_u32(&out->data[6], (uint32_t)in->velocity_mdeg_s);
    put_u16(&out->data[10], (uint16_t)in->current_ma);
    put_u16(&out->data[12], (uint16_t)in->temperature_c10);
    put_u16(&out->data[14], in->last_command_seq);
    return RJS_PROTO_OK;
}

rjs_proto_result_t rjs_decode_joint_state(const rjs_can_frame_t *in, rjs_joint_state_t *out)
{
    rjs_proto_result_t r = frame_check(in, RJS_MSG_JOINT_STATE, JOINT_STATE_LEN);
    if (r != RJS_PROTO_OK) {
        return r;
    }
    if (out == NULL) {
        return RJS_PROTO_ERR_PARAM;
    }
    out->mode             = in->data[1];
    out->position_mdeg    = (int32_t)get_u32(&in->data[2]);
    out->velocity_mdeg_s  = (int32_t)get_u32(&in->data[6]);
    out->current_ma       = (int16_t)get_u16(&in->data[10]);
    out->temperature_c10  = (int16_t)get_u16(&in->data[12]);
    out->last_command_seq = get_u16(&in->data[14]);
    return RJS_PROTO_OK;
}

/* ---- EMERGENCY ---------------------------------------------------------- */

rjs_proto_result_t rjs_encode_emergency(uint8_t node, const rjs_emergency_t *in, rjs_can_frame_t *out)
{
    if (in == NULL || out == NULL || !node_valid(node)) {
        return RJS_PROTO_ERR_PARAM;
    }
    frame_start(out, RJS_MSG_EMERGENCY, node, EMERGENCY_LEN);
    put_u16(&out->data[2], in->code);
    put_u32(&out->data[4], in->detail);
    return RJS_PROTO_OK;
}

rjs_proto_result_t rjs_decode_emergency(const rjs_can_frame_t *in, rjs_emergency_t *out)
{
    rjs_proto_result_t r = frame_check(in, RJS_MSG_EMERGENCY, EMERGENCY_LEN);
    if (r != RJS_PROTO_OK) {
        return r;
    }
    if (out == NULL) {
        return RJS_PROTO_ERR_PARAM;
    }
    out->code   = get_u16(&in->data[2]);
    out->detail = get_u32(&in->data[4]);
    return RJS_PROTO_OK;
}
