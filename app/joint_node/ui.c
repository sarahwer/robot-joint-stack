/*
 * ui.c - status screen on the 2.0" ST7789 TFT (240x320 = 30 x 20 characters).
 *
 * The task builds the screen from app_get_view() five times per second and
 * flushes it; the text_screen module redraws only the characters that actually
 * changed, so a normal update moves a few hundred bytes over SPI instead of
 * 150 KB. Rendering and drawing happen at a low priority and never block the
 * 1 kHz control loop.
 *
 *   +------------------------------+
 *   | ROBOT JOINT STACK    node 1  |
 *   | CAN FD 1/2 Mbit  up 123 s    |
 *   |                              |
 *   | POSITION      -45.3 deg      |
 *   | [####........]  vel -90 deg/s|
 *   | mode POSITION   seq 1234     |
 *   | ...                          |
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "board.h"
#include "rjs/node_monitor.h"
#include "rjs/text_screen.h"

#define UI_PERIOD_MS 200u
#define BG           RJS_BLACK

static rjs_text_screen_t s_screen;

static const char *state_name(uint8_t state)
{
    switch (state) {
    case RJS_NODE_STATE_BOOT:        return "BOOT";
    case RJS_NODE_STATE_PREOP:       return "PREOP";
    case RJS_NODE_STATE_OPERATIONAL: return "OPER";
    case RJS_NODE_STATE_FAULT:       return "FAULT";
    default:                         return "?";
    }
}

/* Draw one changed character cell: render the glyph, then blit it. */
static void draw_cell(uint8_t row, uint8_t col, char ch, rjs_color_t fg, rjs_color_t bg, void *user)
{
    (void)user;
    static rjs_color_t cell[RJS_FONT_WIDTH * RJS_FONT_HEIGHT];
    rjs_ts_render_cell(ch, fg, bg, cell);
    board_display_blit((uint16_t)(col * RJS_FONT_WIDTH), (uint16_t)(row * RJS_FONT_HEIGHT),
                       RJS_FONT_WIDTH, RJS_FONT_HEIGHT, cell);
}

static void render(const app_view_t *v)
{
    /* Header */
    rjs_ts_set_colors(&s_screen, RJS_BLUE, BG);
    rjs_ts_print_line(&s_screen, 0, 0, "ROBOT JOINT STACK    node %u", v->node_id);
    rjs_ts_set_colors(&s_screen, RJS_GREY, BG);
    rjs_ts_print_line(&s_screen, 1, 0, "CAN FD 1/2 Mbit   up %lus", (unsigned long)(v->uptime_ms / 1000u));

    /* Joint */
    const int32_t pos = v->joint.position_mdeg;
    const uint32_t pos_abs = (uint32_t)(pos < 0 ? -pos : pos);
    rjs_ts_set_colors(&s_screen, RJS_WHITE, BG);
    rjs_ts_print_line(&s_screen, 3, 0, "POSITION %s%lu.%lu deg", pos < 0 ? "-" : "",
                      (unsigned long)(pos_abs / 1000u), (unsigned long)(pos_abs % 1000u / 100u));

    /* Bar: -170..+170 degrees mapped to 0..1000 per mille */
    const int32_t span = 170000;
    int32_t clamped = pos;
    if (clamped > span) {
        clamped = span;
    } else if (clamped < -span) {
        clamped = -span;
    }
    const uint16_t permille = (uint16_t)(((int64_t)clamped + span) * 1000 / (2 * span));
    rjs_ts_set_colors(&s_screen, (v->joint.mode == RJS_MODE_POSITION) ? RJS_GREEN : RJS_GREY, BG);
    rjs_ts_bar(&s_screen, 4, 0, 24, permille);

    rjs_ts_set_colors(&s_screen, RJS_WHITE, BG);
    rjs_ts_print_line(&s_screen, 5, 0, "vel %ld deg/s  mode %s", (long)(v->joint.velocity_mdeg_s / 1000),
                      v->joint.mode == RJS_MODE_POSITION ? "POS" : "OFF");

    /* Peers */
    rjs_ts_set_colors(&s_screen, RJS_GREY, BG);
    rjs_ts_print_line(&s_screen, 7, 0, "NODES ON BUS (%u alive)", v->peer_count);
    for (uint8_t i = 0; i < RJS_VIEW_PEERS; ++i) {
        const uint8_t row = (uint8_t)(8 + i);
        if (i >= v->peer_count) {
            rjs_ts_clear_row(&s_screen, row);
            continue;
        }
        const app_peer_view_t *p = &v->peers[i];
        const bool alive = (p->status == RJS_PEER_ALIVE);
        rjs_ts_set_colors(&s_screen, alive ? RJS_GREEN : RJS_RED, BG);
        rjs_ts_print_line(&s_screen, row, 0, " node %-3u %-6s %s", p->node,
                          alive ? state_name(p->state) : "LOST", (p->error_flags != 0u) ? "ERR" : "");
    }

    /* Bus statistics */
    rjs_ts_set_colors(&s_screen, RJS_GREY, BG);
    rjs_ts_print_line(&s_screen, 13, 0, "BUS");
    rjs_ts_set_colors(&s_screen, RJS_WHITE, BG);
    rjs_ts_print_line(&s_screen, 14, 0, " tx %lu  rx %lu", (unsigned long)v->tx_frames,
                      (unsigned long)v->rx_frames);
    rjs_ts_set_colors(&s_screen, (v->tx_error_count > 0u || v->rx_error_count > 0u) ? RJS_YELLOW : RJS_WHITE, BG);
    rjs_ts_print_line(&s_screen, 15, 0, " tec %u  rec %u  drop %lu", v->tx_error_count, v->rx_error_count,
                      (unsigned long)(v->tx_busy + v->rx_dropped));
    rjs_ts_set_colors(&s_screen, RJS_WHITE, BG);
    rjs_ts_print_line(&s_screen, 16, 0, " loop jitter %lu us", (unsigned long)v->loop_jitter_max_us);

    /* Status line */
    if (v->bus_off) {
        rjs_ts_set_colors(&s_screen, RJS_WHITE, RJS_RED);
        rjs_ts_print_line(&s_screen, 18, 0, " CAN BUS OFF - recovering ");
    } else if ((v->joint_flags & RJS_ERR_PEER_LOST) != 0u) {
        rjs_ts_set_colors(&s_screen, RJS_BLACK, RJS_YELLOW);
        rjs_ts_print_line(&s_screen, 18, 0, " COMMAND TIMEOUT - joint off ");
    } else if (v->controller && !v->sweep_enabled) {
        rjs_ts_set_colors(&s_screen, RJS_BLACK, RJS_YELLOW);
        rjs_ts_print_line(&s_screen, 18, 0, " SWEEP STOPPED (button B1) ");
    } else {
        rjs_ts_set_colors(&s_screen, RJS_GREEN, BG);
        rjs_ts_print_line(&s_screen, 18, 0, " RUNNING");
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    if (!board_display_init()) {
        vTaskDelete(NULL);
        return;
    }
    rjs_ts_init(&s_screen, RJS_WHITE, BG);

    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        app_view_t view;
        app_get_view(&view);
        render(&view);
        (void)rjs_ts_flush(&s_screen, draw_cell, NULL);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(UI_PERIOD_MS));
    }
}

void ui_start(uint32_t task_priority)
{
    /* Large stack: the display init runs inside the task and uses the HAL. */
    BaseType_t ok = xTaskCreate(ui_task, "ui", 768, NULL, (UBaseType_t)task_priority, NULL);
    configASSERT(ok == pdPASS);
    (void)ok;
}
