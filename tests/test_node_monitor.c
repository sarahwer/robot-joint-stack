/*
 * test_node_monitor.c - join / loss / reboot detection and timer wrap-around.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "rjs/node_monitor.h"
#include "test_framework.h"

typedef struct {
    int joined;
    int lost;
    int rebooted;
    uint8_t last_node;
} events_t;

static void on_event(rjs_monitor_event_t ev, const rjs_peer_t *peer, void *user)
{
    events_t *e = (events_t *)user;
    e->last_node = peer->node;
    if (ev == RJS_EVENT_NODE_JOINED) {
        e->joined++;
    } else if (ev == RJS_EVENT_NODE_LOST) {
        e->lost++;
    } else if (ev == RJS_EVENT_NODE_REBOOTED) {
        e->rebooted++;
    }
}

static rjs_heartbeat_t hb_at(uint32_t uptime)
{
    rjs_heartbeat_t hb;
    memset(&hb, 0, sizeof(hb));
    hb.state = RJS_NODE_STATE_OPERATIONAL;
    hb.uptime_ms = uptime;
    return hb;
}

static void test_join_and_timeout(void)
{
    rjs_monitor_t m;
    events_t e = {0};
    rjs_monitor_init(&m, 300, on_event, &e);

    rjs_heartbeat_t hb = hb_at(1000);
    CHECK(rjs_monitor_on_heartbeat(&m, 2, &hb, 1000));
    CHECK_EQ(e.joined, 1);
    CHECK_EQ(rjs_monitor_alive_count(&m), 1);

    rjs_monitor_poll(&m, 1300);        /* exactly at the limit: still alive */
    CHECK_EQ(e.lost, 0);
    rjs_monitor_poll(&m, 1301);
    CHECK_EQ(e.lost, 1);
    CHECK_EQ(rjs_monitor_find(&m, 2)->status, RJS_PEER_LOST);
    rjs_monitor_poll(&m, 5000);        /* LOST is reported only once */
    CHECK_EQ(e.lost, 1);

    hb = hb_at(5000);
    CHECK(rjs_monitor_on_heartbeat(&m, 2, &hb, 5000));
    CHECK_EQ(e.joined, 2);             /* came back */
    CHECK_EQ(rjs_monitor_alive_count(&m), 1);
}

static void test_reboot_detection(void)
{
    rjs_monitor_t m;
    events_t e = {0};
    rjs_monitor_init(&m, 300, on_event, &e);
    rjs_heartbeat_t hb = hb_at(60000);
    rjs_monitor_on_heartbeat(&m, 4, &hb, 100);
    hb = hb_at(60100);
    rjs_monitor_on_heartbeat(&m, 4, &hb, 200);
    CHECK_EQ(e.rebooted, 0);
    hb = hb_at(50);                    /* uptime restarted: the node was reset */
    rjs_monitor_on_heartbeat(&m, 4, &hb, 300);
    CHECK_EQ(e.rebooted, 1);
    CHECK_EQ(e.last_node, 4);
}

static void test_timer_wraparound(void)
{
    rjs_monitor_t m;
    events_t e = {0};
    rjs_monitor_init(&m, 300, on_event, &e);
    rjs_heartbeat_t hb = hb_at(1);
    const uint32_t t0 = 0xFFFFFF00u;   /* 256 ms before the 32-bit counter wraps */
    rjs_monitor_on_heartbeat(&m, 1, &hb, t0);
    rjs_monitor_poll(&m, t0 + 200u);   /* wrapped, only 200 ms elapsed */
    CHECK_EQ(e.lost, 0);
    rjs_monitor_poll(&m, t0 + 400u);
    CHECK_EQ(e.lost, 1);
}

static void test_invalid_and_full_table(void)
{
    rjs_monitor_t m;
    rjs_monitor_init(&m, 100, NULL, NULL);
    rjs_heartbeat_t hb = hb_at(0);
    CHECK(!rjs_monitor_on_heartbeat(&m, 0, &hb, 0));
    CHECK(!rjs_monitor_on_heartbeat(&m, 200, &hb, 0));
    for (uint8_t n = 1; n <= RJS_MONITOR_MAX_NODES; ++n) {
        CHECK(rjs_monitor_on_heartbeat(&m, n, &hb, 0));
    }
    CHECK(!rjs_monitor_on_heartbeat(&m, 100, &hb, 0));   /* table full */
    CHECK(rjs_monitor_on_heartbeat(&m, 1, &hb, 10));     /* known node still OK */
    CHECK(rjs_monitor_find(&m, 100) == NULL);
}

int main(void)
{
    RUN(test_join_and_timeout);
    RUN(test_reboot_detection);
    RUN(test_timer_wraparound);
    RUN(test_invalid_and_full_table);
    TEST_MAIN_END();
}
