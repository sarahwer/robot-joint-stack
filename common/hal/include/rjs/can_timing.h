/*
 * can_timing.h - compute CAN bit timing from a kernel clock and a bitrate.
 *
 * Every CAN controller describes a bit as  1 (sync) + tseg1 + tseg2  time
 * quanta, with a prescaler dividing the kernel clock. Only the register limits
 * differ between vendors, so each port passes its own limits and gets
 * register-ready values back. Pure function, unit tested on the host.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RJS_CAN_TIMING_H
#define RJS_CAN_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t prescaler_max;
    uint32_t tseg1_max;
    uint32_t tseg2_max;
    uint32_t sjw_max;
} rjs_can_timing_limits_t;

typedef struct {
    uint32_t prescaler;
    uint32_t tseg1;              /**< propagation + phase segment 1, in time quanta */
    uint32_t tseg2;              /**< phase segment 2, in time quanta */
    uint32_t sjw;
    uint16_t sample_point_permille; /**< achieved sample point, e.g. 800 = 80.0 % */
} rjs_can_timing_t;

/**
 * Find the timing with the most time quanta per bit (best resolution) that
 * gives the exact bitrate and a sample point close to the request.
 * Returns false if no exact solution exists within the limits.
 */
bool rjs_can_calc_timing(uint32_t clock_hz, uint32_t bitrate, uint16_t sample_point_permille,
                         const rjs_can_timing_limits_t *limits, rjs_can_timing_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RJS_CAN_TIMING_H */
