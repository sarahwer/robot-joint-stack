/*
 * app.c - joint node application.
 *
 * Task layout (higher number = higher priority):
 *
 *   control  (5)  1 kHz   joint model step, JOINT_STATE every 10 ms, jitter measurement
 *   can_rx   (4)  event   decode received frames, node supervision
 *   comms    (3)  10 Hz   HEARTBEAT; on the controller node also COMMAND + user button
 *   stats    (2)  1 Hz    one status line on the UART
 *   log      (1)  event   UART output (see log.c)
 *
 * Ownership rules keep the design free of shared-data races:
 *   - only the control task touches the joint model (commands reach it through a queue),
 *   - only the can_rx task touches the node monitor,
 *   - everything else reads short snapshots copied inside a critical section.
 *
 * SPDX-License-Identifier: MIT
 */
#include "app.h"

#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "board.h"
#include "log.h"
#include "rjs/hal_can.h"
#include "rjs/joint.h"
#include "rjs/node_monitor.h"
#include "rjs/protocol.h"

#define FW_MAJOR 0u
#define FW_MINOR 1u
#define FW_PATCH 0u

#define PRIO_CONTROL 5u
#define PRIO_CAN_RX  4u
#define PRIO_COMMS   3u
#define PRIO_STATS   2u
#define PRIO_LOG     1u

#define CONTROL_PERIOD_MS      1u
#define JOINT_STATE_PERIOD_MS 10u
#define HEARTBEAT_PERIOD_MS  100u
#define PEER_TIMEOUT_MS      350u    /* 3 missed heartbeats + margin */
#define SWEEP_HALF_PERIOD_MS 3000u
#define SWEEP_AMPLITUDE_MDEG 90000
#define SWEEP_VELOCITY_MDEG_S 90000

#define RX_QUEUE_DEPTH  32u
#define CMD_QUEUE_DEPTH  4u

/* ---- shared state ---------------------------------------------------- */

typedef struct {
    volatile uint32_t rx_frames;
    volatile uint32_t rx_dropped;       /* ISR could not queue the frame */
    volatile uint32_t rx_decode_errors;
    volatile uint32_t tx_frames;
    volatile uint32_t tx_busy;          /* TX FIFO full, frame dropped */
    volatile uint32_t loop_count;
    volatile uint32_t loop_jitter_max_us;
    volatile uint32_t loop_overruns;    /* loop started more than one period late */
} app_stats_t;

static app_stats_t       s_stats;
static QueueHandle_t     s_rx_queue;
static QueueHandle_t     s_cmd_queue;
static SemaphoreHandle_t s_tx_mutex;
static rjs_monitor_t     s_monitor;           /* owned by can_rx task */
static rjs_joint_state_t s_joint_snapshot;    /* written by control task */
static uint16_t          s_joint_flags;       /* written by control task */
static uint8_t           s_alive_peers;       /* written by can_rx task */
static app_peer_view_t   s_peer_view[RJS_VIEW_PEERS];  /* written by can_rx task */
static uint8_t           s_peer_view_count;
static volatile bool     s_sweep_enabled = true;
static uint8_t           s_node_id;

/* ---- CAN helpers ------------------------------------------------------- */

static void can_rx_isr(const rjs_can_frame_t *frame, void *user)
{
    (void)user;
    BaseType_t woken = pdFALSE;
    if (s_rx_queue == NULL || xQueueSendFromISR(s_rx_queue, frame, &woken) != pdTRUE) {
        s_stats.rx_dropped++;
    }
    portYIELD_FROM_ISR(woken);
}

static void can_send(const rjs_can_frame_t *frame)
{
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    rjs_can_result_t r = rjs_can_send(frame);
    xSemaphoreGive(s_tx_mutex);
    if (r == RJS_CAN_OK) {
        s_stats.tx_frames++;
    } else {
        s_stats.tx_busy++;
    }
}

/* ---- control task: joint model at 1 kHz ------------------------------------ */

static void control_task(void *arg)
{
    (void)arg;
    const rjs_joint_config_t cfg = {
        .min_mdeg = -170000,
        .max_mdeg = 170000,
        .accel_mdeg_s2 = 360000,
        .command_timeout_ms = 500,
    };
    rjs_joint_t joint;
    rjs_joint_init(&joint, &cfg);

    const uint32_t cyc_per_us = board_cycles_per_us();
    uint32_t last_cycles = board_cycles();
    uint32_t ticks = 0;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
        board_debug_pin_toggle();

        /* Loop timing: deviation of the actual period from 1000 us. */
        const uint32_t now = board_cycles();
        const uint32_t period_us = (now - last_cycles) / cyc_per_us;
        last_cycles = now;
        if (ticks > 10u) {   /* skip start-up */
            const uint32_t dev = period_us > 1000u ? period_us - 1000u : 1000u - period_us;
            if (dev > s_stats.loop_jitter_max_us) {
                s_stats.loop_jitter_max_us = dev;
            }
            if (period_us > 2000u) {
                s_stats.loop_overruns++;
            }
        }

        rjs_command_t cmd;
        while (xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
            rjs_joint_command(&joint, &cmd);
        }
        rjs_joint_step(&joint, CONTROL_PERIOD_MS);

        rjs_joint_state_t st;
        rjs_joint_get_state(&joint, &st);
        taskENTER_CRITICAL();
        s_joint_snapshot = st;
        s_joint_flags    = joint.error_flags;
        taskEXIT_CRITICAL();

        if (++ticks % JOINT_STATE_PERIOD_MS == 0u) {
            rjs_can_frame_t f;
            if (rjs_encode_joint_state(s_node_id, &st, &f) == RJS_PROTO_OK) {
                can_send(&f);
            }
        }
        s_stats.loop_count++;
    }
}

/* ---- can_rx task: decode and supervise ------------------------------------ */

static void on_monitor_event(rjs_monitor_event_t ev, const rjs_peer_t *peer, void *user)
{
    (void)user;
    switch (ev) {
    case RJS_EVENT_NODE_JOINED:
        rjs_log("[bus] node %u joined (fw %u.%u.%u)", peer->node, peer->last.fw_major,
                peer->last.fw_minor, peer->last.fw_patch);
        break;
    case RJS_EVENT_NODE_LOST:
        rjs_log("[bus] node %u LOST (no heartbeat for %u ms)", peer->node, (unsigned)PEER_TIMEOUT_MS);
        break;
    case RJS_EVENT_NODE_REBOOTED:
        rjs_log("[bus] node %u rebooted", peer->node);
        break;
    default:
        break;
    }
}

static void handle_frame(const rjs_can_frame_t *f)
{
    const uint8_t node = rjs_id_node(f->id);
    switch (rjs_id_type(f->id)) {
    case RJS_MSG_HEARTBEAT: {
        rjs_heartbeat_t hb;
        if (rjs_decode_heartbeat(f, &hb) == RJS_PROTO_OK) {
            rjs_monitor_on_heartbeat(&s_monitor, node, &hb, board_millis());
        } else {
            s_stats.rx_decode_errors++;
        }
        break;
    }
    case RJS_MSG_COMMAND: {
        if (node != s_node_id && node != RJS_NODE_BROADCAST) {
            break;   /* not for us */
        }
        rjs_command_t cmd;
        if (rjs_decode_command(f, &cmd) == RJS_PROTO_OK) {
            (void)xQueueSend(s_cmd_queue, &cmd, 0);
        } else {
            s_stats.rx_decode_errors++;
        }
        break;
    }
    case RJS_MSG_EMERGENCY: {
        rjs_emergency_t em;
        if (rjs_decode_emergency(f, &em) == RJS_PROTO_OK) {
            rjs_log("[bus] EMERGENCY from node %u: code 0x%04x detail 0x%08lx", node, em.code,
                    (unsigned long)em.detail);
        }
        break;
    }
    case RJS_MSG_JOINT_STATE:
    case RJS_MSG_SENSOR:
    default:
        break;   /* observed by other nodes / tools, nothing to do here yet */
    }
}

static void can_rx_task(void *arg)
{
    (void)arg;
    rjs_monitor_init(&s_monitor, PEER_TIMEOUT_MS, on_monitor_event, NULL);
    rjs_can_frame_t f;
    for (;;) {
        if (xQueueReceive(s_rx_queue, &f, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_stats.rx_frames++;
            handle_frame(&f);
        }
        rjs_monitor_poll(&s_monitor, board_millis());
        s_alive_peers = rjs_monitor_alive_count(&s_monitor);

        /* Publish a small snapshot of the peer table for the display. */
        app_peer_view_t view[RJS_VIEW_PEERS];
        uint8_t n = 0;
        for (uint8_t i = 0; i < s_monitor.count && n < RJS_VIEW_PEERS; ++i) {
            view[n].node        = s_monitor.peers[i].node;
            view[n].status      = (uint8_t)s_monitor.peers[i].status;
            view[n].state       = s_monitor.peers[i].last.state;
            view[n].error_flags = s_monitor.peers[i].last.error_flags;
            n++;
        }
        taskENTER_CRITICAL();
        for (uint8_t i = 0; i < n; ++i) {
            s_peer_view[i] = view[i];
        }
        s_peer_view_count = n;
        taskEXIT_CRITICAL();
    }
}

/* ---- comms task: heartbeat, commands, button -------------------------------- */

static uint16_t heartbeat_flags(void)
{
    rjs_can_status_t cs;
    rjs_can_get_status(&cs);
    uint16_t flags;
    taskENTER_CRITICAL();
    flags = s_joint_flags;
    taskEXIT_CRITICAL();
    if (cs.error_passive) {
        flags |= RJS_ERR_CAN_PASSIVE;
    }
    if (cs.bus_off) {
        flags |= RJS_ERR_CAN_BUS_OFF;
    }
    if (s_stats.tx_busy != 0u) {
        flags |= RJS_ERR_TX_DROPPED;
    }
    return flags;
}

static void comms_task(void *arg)
{
    (void)arg;
    const bool controller = (s_node_id == RJS_CONTROLLER_NODE_ID);
    uint16_t seq = 0;
    int32_t target = SWEEP_AMPLITUDE_MDEG;
    uint32_t last_flip = board_millis();
    bool button_was_down = false;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
        board_led_status_toggle();

        /* Heartbeat */
        rjs_joint_state_t js;
        taskENTER_CRITICAL();
        js = s_joint_snapshot;
        taskEXIT_CRITICAL();
        const uint16_t flags = heartbeat_flags();
        rjs_heartbeat_t hb = {
            .state = (flags & RJS_ERR_CAN_BUS_OFF) ? RJS_NODE_STATE_FAULT
                     : (js.mode == RJS_MODE_POSITION) ? RJS_NODE_STATE_OPERATIONAL
                                                     : RJS_NODE_STATE_PREOP,
            .error_flags = flags,
            .uptime_ms = board_millis(),
            .fw_major = FW_MAJOR, .fw_minor = FW_MINOR, .fw_patch = FW_PATCH,
        };
        rjs_can_frame_t f;
        if (rjs_encode_heartbeat(s_node_id, &hb, &f) == RJS_PROTO_OK) {
            can_send(&f);
        }
        board_led_error((flags & (RJS_ERR_CAN_BUS_OFF | RJS_ERR_CAN_PASSIVE | RJS_ERR_PEER_LOST)) != 0u);

        if (!controller) {
            continue;
        }

        /* User button toggles the sweep. Stopping it shows the command timeout:
         * every joint disables itself 500 ms after the last command. */
        const bool down = board_button_raw();
        if (down && !button_was_down) {
            s_sweep_enabled = !s_sweep_enabled;
            rjs_log("[ctrl] sweep %s", s_sweep_enabled ? "ENABLED" : "STOPPED (joints will time out)");
        }
        button_was_down = down;

        if (!s_sweep_enabled) {
            continue;
        }
        if ((uint32_t)(board_millis() - last_flip) >= SWEEP_HALF_PERIOD_MS) {
            last_flip = board_millis();
            target = -target;
        }
        rjs_command_t cmd = {
            .mode = RJS_MODE_POSITION,
            .target_mdeg = target,
            .max_velocity_mdeg_s = SWEEP_VELOCITY_MDEG_S,
            .sequence = ++seq,
        };
        if (rjs_encode_command(RJS_NODE_BROADCAST, &cmd, &f) == RJS_PROTO_OK) {
            can_send(&f);
            (void)xQueueSend(s_cmd_queue, &cmd, 0);   /* the controller is also a joint */
        }
    }
}

/* ---- stats task --------------------------------------------------------------- */

static void stats_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
        rjs_joint_state_t js;
        taskENTER_CRITICAL();
        js = s_joint_snapshot;
        taskEXIT_CRITICAL();
        rjs_can_status_t cs;
        rjs_can_get_status(&cs);

        const int32_t pos = js.position_mdeg;
        const uint32_t pos_abs = (uint32_t)(pos < 0 ? -pos : pos);

        rjs_log("[node %u] up %lus | peers %u | pos %s%lu.%lu deg vel %ld deg/s | "
                "tx %lu rx %lu drop %lu/%lu | tec %u rec %u | jitter %lu us",
                s_node_id, (unsigned long)(board_millis() / 1000u), s_alive_peers,
                pos < 0 ? "-" : "", (unsigned long)(pos_abs / 1000u), (unsigned long)(pos_abs % 1000u / 100u),
                (long)(js.velocity_mdeg_s / 1000),
                (unsigned long)s_stats.tx_frames, (unsigned long)s_stats.rx_frames,
                (unsigned long)s_stats.tx_busy, (unsigned long)s_stats.rx_dropped,
                cs.tx_error_count, cs.rx_error_count,
                (unsigned long)s_stats.loop_jitter_max_us);
    }
}

/* ---- snapshot for the display ---------------------------------------------------- */

void app_get_view(app_view_t *v)
{
    memset(v, 0, sizeof(*v));
    v->node_id       = s_node_id;
    v->controller    = (s_node_id == RJS_CONTROLLER_NODE_ID);
    v->sweep_enabled = s_sweep_enabled;
    v->uptime_ms     = board_millis();

    taskENTER_CRITICAL();
    v->joint       = s_joint_snapshot;
    v->joint_flags = s_joint_flags;
    v->peer_count  = s_peer_view_count;
    for (uint8_t i = 0; i < s_peer_view_count && i < RJS_VIEW_PEERS; ++i) {
        v->peers[i] = s_peer_view[i];
    }
    taskEXIT_CRITICAL();

    v->tx_frames          = s_stats.tx_frames;
    v->rx_frames          = s_stats.rx_frames;
    v->tx_busy            = s_stats.tx_busy;
    v->rx_dropped         = s_stats.rx_dropped;
    v->loop_jitter_max_us = s_stats.loop_jitter_max_us;

    rjs_can_status_t cs;
    rjs_can_get_status(&cs);
    v->tx_error_count = cs.tx_error_count;
    v->rx_error_count = cs.rx_error_count;
    v->bus_off        = cs.bus_off;
}

/* ---- start-up ------------------------------------------------------------------ */

void app_start(void)
{
    s_node_id = board_node_id();

    /* CAN first: HAL init must run before the first FreeRTOS object is created
     * (FreeRTOS masks interrupts, and with them the HAL tick, until the
     * scheduler starts). Frames arriving before the queue exists are counted. */
    const rjs_can_config_t can_cfg = {.nominal_bitrate = 1000000u, .data_bitrate = 2000000u};
    if (rjs_can_init(&can_cfg, can_rx_isr, NULL) != RJS_CAN_OK) {
        board_fatal("CAN init failed");
    }

    s_rx_queue  = xQueueCreate(RX_QUEUE_DEPTH, sizeof(rjs_can_frame_t));
    s_cmd_queue = xQueueCreate(CMD_QUEUE_DEPTH, sizeof(rjs_command_t));
    s_tx_mutex  = xSemaphoreCreateMutex();
    configASSERT(s_rx_queue != NULL && s_cmd_queue != NULL && s_tx_mutex != NULL);

    rjs_log_start(PRIO_LOG);
    rjs_log("%s", "");
    rjs_log("robot-joint-stack %u.%u.%u on %s, node %u%s", FW_MAJOR, FW_MINOR, FW_PATCH, board_name(),
            s_node_id, s_node_id == RJS_CONTROLLER_NODE_ID ? " (controller)" : "");
    rjs_log("CAN FD 1 Mbit/s nominal, 2 Mbit/s data (BRS)");

    BaseType_t ok = pdPASS;
    ok &= xTaskCreate(control_task, "control", 512, NULL, PRIO_CONTROL, NULL);
    ok &= xTaskCreate(can_rx_task, "can_rx", 512, NULL, PRIO_CAN_RX, NULL);
    ok &= xTaskCreate(comms_task, "comms", 512, NULL, PRIO_COMMS, NULL);
    ok &= xTaskCreate(stats_task, "stats", 512, NULL, PRIO_STATS, NULL);
    if (ok != pdPASS) {
        board_fatal("task creation failed");
    }
#if RJS_ENABLE_DISPLAY
    ui_start(PRIO_STATS);
#endif
}
