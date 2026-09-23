/*
 * joint.h - simulated robot joint with the safety behaviour of a real one.
 *
 * Until a motor is connected, each node runs this model: it moves towards
 * the commanded target with a velocity and acceleration limit, clamps
 * targets to soft limits, and disables itself when commands stop arriving
 * (command timeout). Replacing the model with a real motor driver later only
 * changes the inside of rjs_joint_step(); the CAN interface stays the same.
 *
 * Pure integer math, no RTOS or hardware dependency.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RJS_JOINT_H
#define RJS_JOINT_H

#include <stdbool.h>
#include <stdint.h>

#include "rjs/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t  min_mdeg;              /**< soft limit, e.g. -170000 */
    int32_t  max_mdeg;              /**< soft limit, e.g. +170000 */
    int32_t  accel_mdeg_s2;         /**< acceleration limit (> 0) */
    uint32_t command_timeout_ms;    /**< 0 = no timeout */
} rjs_joint_config_t;

typedef struct {
    rjs_joint_config_t cfg;
    uint8_t  mode;                  /**< rjs_joint_mode_t */
    int32_t  position_mdeg;
    int32_t  velocity_mdeg_s;
    int32_t  target_mdeg;
    int32_t  max_velocity_mdeg_s;
    uint16_t last_seq;
    uint32_t since_command_ms;
    uint16_t error_flags;           /**< RJS_ERR_* raised by the joint */
    uint32_t timeouts;              /**< number of command timeouts so far */
} rjs_joint_t;

void rjs_joint_init(rjs_joint_t *j, const rjs_joint_config_t *cfg);

/** Apply a decoded COMMAND. Targets outside the soft limits are clamped and flagged. */
void rjs_joint_command(rjs_joint_t *j, const rjs_command_t *cmd);

/** Advance the model by dt_ms (call from the control loop). */
void rjs_joint_step(rjs_joint_t *j, uint32_t dt_ms);

/** Fill a JOINT_STATE message from the current model state. */
void rjs_joint_get_state(const rjs_joint_t *j, rjs_joint_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RJS_JOINT_H */
