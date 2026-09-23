/*
 * app.h - joint node application (FreeRTOS tasks on top of the common libraries).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RJS_APP_H
#define RJS_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/** Node that acts as the bus controller and sends the motion commands. */
#define RJS_CONTROLLER_NODE_ID 1u

/** Initialise CAN, create queues and tasks. Call after board_init(), before vTaskStartScheduler(). */
void app_start(void);

#ifdef __cplusplus
}
#endif

#endif /* RJS_APP_H */
