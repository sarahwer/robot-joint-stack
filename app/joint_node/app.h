/*
 * app.h - joint node application (FreeRTOS tasks on top of the common libraries).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RJS_APP_H
#define RJS_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "rjs/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Node that acts as the bus controller and sends the motion commands. */
#define RJS_CONTROLLER_NODE_ID 1u

/** Initialise CAN, create queues and tasks. Call after board_init(), before vTaskStartScheduler(). */
void app_start(void);

#define RJS_VIEW_PEERS 4u

/** One peer as shown on the display. */
typedef struct {
    uint8_t node;
    uint8_t status;          /**< rjs_peer_status_t */
    uint8_t state;           /**< rjs_node_state_t from its last heartbeat */
    uint16_t error_flags;
} app_peer_view_t;

/** Consistent snapshot of everything the UI shows. */
typedef struct {
    uint8_t  node_id;
    bool     controller;
    bool     sweep_enabled;
    uint32_t uptime_ms;
    uint8_t  peer_count;
    app_peer_view_t peers[RJS_VIEW_PEERS];
    rjs_joint_state_t joint;
    uint16_t joint_flags;
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t tx_busy;
    uint32_t rx_dropped;
    uint8_t  tx_error_count;
    uint8_t  rx_error_count;
    bool     bus_off;
    uint32_t loop_jitter_max_us;
} app_view_t;

/** Fill the snapshot (safe to call from any task). */
void app_get_view(app_view_t *v);

/** Display task; created by app_start() when the display is enabled. */
void ui_start(uint32_t task_priority);

#ifdef __cplusplus
}
#endif

#endif /* RJS_APP_H */
