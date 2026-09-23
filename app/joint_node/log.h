/*
 * log.h - non-blocking logging for FreeRTOS tasks.
 *
 * rjs_log() formats the line in the caller's context and hands it to a
 * low-priority log task, which writes it to the UART. The caller never waits
 * for the UART, so logging from the control loop cannot break its timing;
 * if the queue is full the line is dropped and counted instead.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RJS_LOG_H
#define RJS_LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RJS_LOG_LINE_MAX 120u

/** Create the queue and the log task. Call before vTaskStartScheduler(). */
void rjs_log_start(uint32_t task_priority);

/** printf-style, adds "\r\n". Safe from any task (not from interrupts). */
void rjs_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** Number of lines dropped because the queue was full. */
uint32_t rjs_log_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* RJS_LOG_H */
