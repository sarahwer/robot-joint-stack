/*
 * can_timing.c - bit timing calculation shared by all CAN ports.
 *
 * SPDX-License-Identifier: MIT
 */
#include "rjs/can_timing.h"

#include <stddef.h>

bool rjs_can_calc_timing(uint32_t clock_hz, uint32_t bitrate, uint16_t sample_point_permille,
                         const rjs_can_timing_limits_t *limits, rjs_can_timing_t *out)
{
    if (limits == NULL || out == NULL || bitrate == 0u || clock_hz < bitrate ||
        sample_point_permille < 500u || sample_point_permille > 950u) {
        return false;
    }

    /* Smallest prescaler first = most time quanta per bit. */
    for (uint32_t presc = 1; presc <= limits->prescaler_max; ++presc) {
        const uint64_t denom = (uint64_t)presc * bitrate;
        if ((clock_hz % denom) != 0u) {
            continue;                    /* bitrate would not be exact */
        }
        const uint32_t total = (uint32_t)(clock_hz / denom);   /* quanta per bit */
        if (total < 4u) {
            break;                       /* only gets smaller with larger prescalers */
        }

        /* tseg2 from the sample point, rounded to the nearest quantum. */
        uint32_t tseg2 = (uint32_t)(((uint64_t)total * (1000u - sample_point_permille) + 500u) / 1000u);
        if (tseg2 < 1u) {
            tseg2 = 1u;
        }
        const uint32_t tseg1 = total - 1u - tseg2;
        if (tseg1 < 1u || tseg1 > limits->tseg1_max || tseg2 > limits->tseg2_max) {
            continue;
        }

        out->prescaler = presc;
        out->tseg1     = tseg1;
        out->tseg2     = tseg2;
        out->sjw       = (tseg2 < limits->sjw_max) ? tseg2 : limits->sjw_max;
        out->sample_point_permille = (uint16_t)((1000u * (1u + tseg1)) / total);
        return true;
    }
    return false;
}
