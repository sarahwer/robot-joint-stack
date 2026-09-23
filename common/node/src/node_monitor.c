/*
 * node_monitor.c - heartbeat supervision of bus peers.
 *
 * Time comparisons use unsigned subtraction, so they stay correct when the
 * 32-bit millisecond counter wraps around (after ~49.7 days).
 *
 * SPDX-License-Identifier: MIT
 */
#include "rjs/node_monitor.h"

#include <string.h>

static void emit(rjs_monitor_t *m, rjs_monitor_event_t ev, const rjs_peer_t *p)
{
    if (m->cb != NULL) {
        m->cb(ev, p, m->user);
    }
}

static rjs_peer_t *find_mut(rjs_monitor_t *m, uint8_t node)
{
    for (uint8_t i = 0; i < m->count; ++i) {
        if (m->peers[i].node == node) {
            return &m->peers[i];
        }
    }
    return NULL;
}

void rjs_monitor_init(rjs_monitor_t *m, uint32_t timeout_ms, rjs_monitor_cb_t cb, void *user)
{
    memset(m, 0, sizeof(*m));
    m->timeout_ms = timeout_ms;
    m->cb         = cb;
    m->user       = user;
}

bool rjs_monitor_on_heartbeat(rjs_monitor_t *m, uint8_t node, const rjs_heartbeat_t *hb, uint32_t now_ms)
{
    if (m == NULL || hb == NULL || node == RJS_NODE_BROADCAST || node > RJS_NODE_ID_MAX) {
        return false;
    }

    rjs_peer_t *p = find_mut(m, node);
    if (p == NULL) {
        if (m->count >= RJS_MONITOR_MAX_NODES) {
            return false;
        }
        p = &m->peers[m->count++];
        memset(p, 0, sizeof(*p));
        p->node = node;
    }

    const rjs_peer_status_t before = p->status;
    const bool rebooted = (before == RJS_PEER_ALIVE) && (hb->uptime_ms < p->last.uptime_ms);

    p->last         = *hb;
    p->last_seen_ms = now_ms;
    p->status       = RJS_PEER_ALIVE;
    p->heartbeats++;

    if (before != RJS_PEER_ALIVE) {
        emit(m, RJS_EVENT_NODE_JOINED, p);
    } else if (rebooted) {
        emit(m, RJS_EVENT_NODE_REBOOTED, p);
    }
    return true;
}

void rjs_monitor_poll(rjs_monitor_t *m, uint32_t now_ms)
{
    if (m == NULL) {
        return;
    }
    for (uint8_t i = 0; i < m->count; ++i) {
        rjs_peer_t *p = &m->peers[i];
        if (p->status == RJS_PEER_ALIVE && (uint32_t)(now_ms - p->last_seen_ms) > m->timeout_ms) {
            p->status = RJS_PEER_LOST;
            emit(m, RJS_EVENT_NODE_LOST, p);
        }
    }
}

const rjs_peer_t *rjs_monitor_find(const rjs_monitor_t *m, uint8_t node)
{
    if (m == NULL) {
        return NULL;
    }
    for (uint8_t i = 0; i < m->count; ++i) {
        if (m->peers[i].node == node) {
            return &m->peers[i];
        }
    }
    return NULL;
}

uint8_t rjs_monitor_alive_count(const rjs_monitor_t *m)
{
    uint8_t n = 0;
    if (m == NULL) {
        return 0;
    }
    for (uint8_t i = 0; i < m->count; ++i) {
        if (m->peers[i].status == RJS_PEER_ALIVE) {
            n++;
        }
    }
    return n;
}
