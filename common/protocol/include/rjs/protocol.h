/*
 * protocol.h - robot-joint-stack CAN FD application protocol (v1).
 *
 * Identifier layout (11-bit standard ID), lower value = higher bus priority:
 *
 *     bit 10..7   message type  (4 bits)
 *     bit  6..0   node ID       (7 bits, 1..127, 0 = broadcast)
 *
 * All multi-byte fields are little-endian and packed byte by byte, so the
 * encoding does not depend on compiler struct layout or CPU endianness.
 * Every payload starts with the protocol version byte.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RJS_PROTOCOL_H
#define RJS_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rjs/hal_can.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RJS_PROTOCOL_VERSION 1u
#define RJS_NODE_BROADCAST   0u
#define RJS_NODE_ID_MAX      127u

/** Message types, ordered by priority (EMERGENCY wins arbitration). */
typedef enum {
    RJS_MSG_EMERGENCY   = 0x0,
    RJS_MSG_COMMAND     = 0x1,
    RJS_MSG_JOINT_STATE = 0x2,
    RJS_MSG_SENSOR      = 0x3,
    RJS_MSG_HEARTBEAT   = 0x7,
} rjs_msg_type_t;

typedef enum {
    RJS_PROTO_OK = 0,
    RJS_PROTO_ERR_PARAM,        /**< NULL pointer or value out of range */
    RJS_PROTO_ERR_LENGTH,       /**< payload shorter than the message needs */
    RJS_PROTO_ERR_VERSION,      /**< unknown protocol version */
    RJS_PROTO_ERR_TYPE,         /**< frame carries a different message type */
} rjs_proto_result_t;

/** Node life cycle, reported in every heartbeat. */
typedef enum {
    RJS_NODE_STATE_BOOT        = 0,
    RJS_NODE_STATE_PREOP       = 1,  /**< running, outputs disabled */
    RJS_NODE_STATE_OPERATIONAL = 2,
    RJS_NODE_STATE_FAULT       = 3,
} rjs_node_state_t;

/** Error flags (bit field) used in heartbeats. */
#define RJS_ERR_NONE          0x0000u
#define RJS_ERR_CAN_PASSIVE   0x0001u
#define RJS_ERR_CAN_BUS_OFF   0x0002u
#define RJS_ERR_TX_DROPPED    0x0004u
#define RJS_ERR_PEER_LOST     0x0008u
#define RJS_ERR_OVER_TEMP     0x0010u
#define RJS_ERR_LIMIT_REACHED 0x0020u

typedef struct {
    uint8_t  state;             /**< rjs_node_state_t */
    uint16_t error_flags;       /**< RJS_ERR_* */
    uint32_t uptime_ms;
    uint8_t  fw_major;
    uint8_t  fw_minor;
    uint8_t  fw_patch;
} rjs_heartbeat_t;

/** Joint mode requested by a COMMAND message. */
typedef enum {
    RJS_MODE_DISABLED = 0,
    RJS_MODE_POSITION = 1,
} rjs_joint_mode_t;

typedef struct {
    uint8_t  mode;                  /**< rjs_joint_mode_t */
    int32_t  target_mdeg;           /**< target position, millidegrees */
    int32_t  max_velocity_mdeg_s;   /**< velocity limit, millidegrees per second (> 0) */
    uint16_t sequence;              /**< incremented by the sender, echoed in JOINT_STATE */
} rjs_command_t;

typedef struct {
    int32_t  position_mdeg;
    int32_t  velocity_mdeg_s;
    int16_t  current_ma;
    int16_t  temperature_c10;       /**< 0.1 degC */
    uint16_t last_command_seq;
    uint8_t  mode;
} rjs_joint_state_t;

typedef struct {
    uint16_t code;
    uint32_t detail;
} rjs_emergency_t;

/* ---- identifier helpers ---------------------------------------------- */

static inline uint16_t rjs_make_id(rjs_msg_type_t type, uint8_t node)
{
    return (uint16_t)((((uint16_t)type & 0x0Fu) << 7) | (node & 0x7Fu));
}

static inline rjs_msg_type_t rjs_id_type(uint16_t id) { return (rjs_msg_type_t)((id >> 7) & 0x0Fu); }
static inline uint8_t        rjs_id_node(uint16_t id) { return (uint8_t)(id & 0x7Fu); }

/* ---- CAN FD length helpers ------------------------------------------- */

/** Smallest valid CAN FD payload size >= len (0..8,12,16,20,24,32,48,64). 0 if len > 64. */
uint8_t rjs_fd_round_len(uint8_t len);
/** CAN FD DLC code (0..15) for a valid FD payload size. 0xFF if len is not a valid size. */
uint8_t rjs_fd_len_to_dlc(uint8_t len);
/** Payload size in bytes for a DLC code (0..15). 0xFF if dlc > 15. */
uint8_t rjs_fd_dlc_to_len(uint8_t dlc);

/* ---- encode / decode --------------------------------------------------- */
/* encode: fills a complete FD frame (id, len, fd, brs, data).                */
/* decode: checks type, length and version before touching the output.       */

rjs_proto_result_t rjs_encode_heartbeat(uint8_t node, const rjs_heartbeat_t *in, rjs_can_frame_t *out);
rjs_proto_result_t rjs_decode_heartbeat(const rjs_can_frame_t *in, rjs_heartbeat_t *out);

rjs_proto_result_t rjs_encode_command(uint8_t target_node, const rjs_command_t *in, rjs_can_frame_t *out);
rjs_proto_result_t rjs_decode_command(const rjs_can_frame_t *in, rjs_command_t *out);

rjs_proto_result_t rjs_encode_joint_state(uint8_t node, const rjs_joint_state_t *in, rjs_can_frame_t *out);
rjs_proto_result_t rjs_decode_joint_state(const rjs_can_frame_t *in, rjs_joint_state_t *out);

rjs_proto_result_t rjs_encode_emergency(uint8_t node, const rjs_emergency_t *in, rjs_can_frame_t *out);
rjs_proto_result_t rjs_decode_emergency(const rjs_can_frame_t *in, rjs_emergency_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RJS_PROTOCOL_H */
