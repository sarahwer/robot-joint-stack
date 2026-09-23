/*
 * node_monitor.h - tracks which nodes on the bus are alive.
 *
 * Feed it every received heartbeat and call rjs_monitor_poll() periodically.
 * A node that stays silent longer than the timeout is reported as LOST once;
 * when it comes back it is reported as ALIVE again. Time is passed in by the
 * caller, so the module has no RTOS or hardware dependency and is fully unit
 * testable on a PC.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RJS_NODE_MONITOR_H
#define RJS_NODE_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "rjs/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RJS_MONITOR_MAX_NODES
#define RJS_MONITOR_MAX_NODES 16u
#endif

typedef enum {
    RJS_PEER_UNKNOWN = 0,   /**< never heard from */
    RJS_PEER_ALIVE,
    RJS_PEER_LOST,
} rjs_peer_status_t;

typedef enum {
    RJS_EVENT_NONE = 0,
    RJS_EVENT_NODE_JOINED,  /**< first heartbeat, or heartbeat after LOST */
    RJS_EVENT_NODE_LOST,    /**< timeout expired */
    RJS_EVENT_NODE_REBOOTED,/**< uptime went backwards: node reset in between */
} rjs_monitor_event_t;

typedef struct {
    uint8_t           node;
    rjs_peer_status_t status;
    uint32_t          last_seen_ms;
    rjs_heartbeat_t   last;
    uint32_t          heartbeats;
} rjs_peer_t;

typedef void (*rjs_monitor_cb_t)(rjs_monitor_event_t event, const rjs_peer_t *peer, void *user);

typedef struct {
    rjs_peer_t       peers[RJS_MONITOR_MAX_NODES];
    uint8_t          count;
    uint32_t         timeout_ms;
    rjs_monitor_cb_t cb;
    void            *user;
} rjs_monitor_t;

void rjs_monitor_init(rjs_monitor_t *m, uint32_t timeout_ms, rjs_monitor_cb_t cb, void *user);

/** Register a heartbeat. Returns false if the table is full or node is invalid. */
bool rjs_monitor_on_heartbeat(rjs_monitor_t *m, uint8_t node, const rjs_heartbeat_t *hb, uint32_t now_ms);

/** Check timeouts. Call periodically (e.g. every 10-100 ms). */
void rjs_monitor_poll(rjs_monitor_t *m, uint32_t now_ms);

/** Look up a peer, NULL if never seen. */
const rjs_peer_t *rjs_monitor_find(const rjs_monitor_t *m, uint8_t node);

/** Number of peers currently ALIVE. */
uint8_t rjs_monitor_alive_count(const rjs_monitor_t *m);

#ifdef __cplusplus
}
#endif

#endif /* RJS_NODE_MONITOR_H */
