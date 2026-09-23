/*
 * board.h - what the application needs from a board port.
 *
 * A new board (NXP i.MX RT1186, Infineon XMC7100, ...) implements this file
 * plus rjs/hal_can.h. Everything in app/ and common/ is reused unchanged.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RJS_BOARD_H
#define RJS_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Clocks, caches, MPU, GPIO, debug UART. Called once before the scheduler starts. */
void board_init(void);

const char *board_name(void);

/** Node ID of this board on the CAN bus (1..127), set at build time. */
uint8_t board_node_id(void);

/** Milliseconds since reset (wraps after ~49 days). */
uint32_t board_millis(void);

/** Free-running CPU cycle counter (e.g. DWT->CYCCNT) for timing measurements. */
uint32_t board_cycles(void);
uint32_t board_cycles_per_us(void);

void board_led_status_toggle(void);    /**< slow blink = alive */
void board_led_error(bool on);
void board_debug_pin_toggle(void);     /**< probe with a logic analyzer: control loop timing */

/** Raw user button level, true = pressed. The app does the debouncing. */
bool board_button_raw(void);

/** Blocking write to the debug UART (ST-LINK virtual COM port). */
void board_uart_write(const char *data, size_t len);

/** Unrecoverable error: disable interrupts, show the error LED, stay here. */
void board_fatal(const char *reason) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* RJS_BOARD_H */
