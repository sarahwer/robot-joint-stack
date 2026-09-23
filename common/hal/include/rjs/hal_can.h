/*
 * hal_can.h - vendor-independent CAN FD interface.
 *
 * Every board port (STM32 FDCAN, NXP FlexCAN, Infineon M_CAN, ...) implements
 * these functions. Application and protocol code only ever include this header,
 * so the same code runs unchanged on every node of the bus.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RJS_HAL_CAN_H
#define RJS_HAL_CAN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RJS_CAN_MAX_PAYLOAD 64u

/** One CAN (FD) frame, independent of any vendor driver structure. */
typedef struct {
    uint16_t id;                          /**< 11-bit standard identifier */
    uint8_t  len;                         /**< payload length in bytes (0..64, FD sizes only) */
    bool     fd;                          /**< true: CAN FD frame, false: classic CAN */
    bool     brs;                         /**< bit rate switch (FD only) */
    uint8_t  data[RJS_CAN_MAX_PAYLOAD];
} rjs_can_frame_t;

/** Bus timing request. The port converts it to its own bit timing registers. */
typedef struct {
    uint32_t nominal_bitrate;             /**< arbitration phase, e.g. 1000000 */
    uint32_t data_bitrate;                /**< data phase (FD + BRS), e.g. 2000000 */
} rjs_can_config_t;

/** Error counters and state, read from the controller. */
typedef struct {
    uint8_t  tx_error_count;
    uint8_t  rx_error_count;
    bool     error_passive;
    bool     bus_off;
    uint32_t tx_dropped;                  /**< frames rejected because the TX FIFO was full */
    uint32_t rx_overrun;                  /**< frames lost because the RX path was full */
} rjs_can_status_t;

typedef enum {
    RJS_CAN_OK = 0,
    RJS_CAN_ERR_PARAM,
    RJS_CAN_ERR_BUSY,                     /**< TX FIFO full, try again later */
    RJS_CAN_ERR_HW,
} rjs_can_result_t;

/**
 * Receive callback. Called from interrupt context by the port:
 * keep it short (e.g. push the frame into an RTOS queue).
 */
typedef void (*rjs_can_rx_cb_t)(const rjs_can_frame_t *frame, void *user);

rjs_can_result_t rjs_can_init(const rjs_can_config_t *cfg, rjs_can_rx_cb_t rx_cb, void *user);
rjs_can_result_t rjs_can_send(const rjs_can_frame_t *frame);
void             rjs_can_get_status(rjs_can_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* RJS_HAL_CAN_H */
