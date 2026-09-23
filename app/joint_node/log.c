/*
 * log.c - queue-based logger, see log.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "board.h"

#define LOG_QUEUE_DEPTH 16u

typedef struct {
    char text[RJS_LOG_LINE_MAX];
} log_line_t;

static QueueHandle_t     s_queue;
static volatile uint32_t s_dropped;

/* Bounded strlen (strnlen is POSIX, not ISO C11). */
static size_t bounded_len(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n] != '\0') {
        n++;
    }
    return n;
}

static void log_task(void *arg)
{
    (void)arg;
    log_line_t line;
    for (;;) {
        if (xQueueReceive(s_queue, &line, portMAX_DELAY) == pdTRUE) {
            board_uart_write(line.text, bounded_len(line.text, sizeof(line.text)));
        }
    }
}

void rjs_log_start(uint32_t task_priority)
{
    s_queue = xQueueCreate(LOG_QUEUE_DEPTH, sizeof(log_line_t));
    configASSERT(s_queue != NULL);
    BaseType_t ok = xTaskCreate(log_task, "log", 384, NULL, (UBaseType_t)task_priority, NULL);
    configASSERT(ok == pdPASS);
    (void)ok;
}

void rjs_log(const char *fmt, ...)
{
    log_line_t line;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line.text, sizeof(line.text) - 2u, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    size_t len = bounded_len(line.text, sizeof(line.text) - 2u);
    line.text[len]      = '\r';
    line.text[len + 1u] = '\n';
    if (len + 2u < sizeof(line.text)) {
        line.text[len + 2u] = '\0';
    }

    if (s_queue == NULL || xQueueSend(s_queue, &line, 0) != pdTRUE) {
        s_dropped++;
    }
}

uint32_t rjs_log_dropped(void)
{
    return s_dropped;
}
