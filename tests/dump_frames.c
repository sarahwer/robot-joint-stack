/*
 * dump_frames.c - print frames from the C encoder in `candump -L` format.
 *
 * CTest pipes this into tools/check_c_python_sync.py, which decodes every line
 * with the Python implementation and compares the values. If someone changes
 * the wire format on one side only, this test fails.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "rjs/protocol.h"

static void print_frame(const rjs_can_frame_t *f)
{
    printf("(0.000000) vcan0 %03X##%d", f->id, f->brs ? 1 : 0);
    for (uint8_t i = 0; i < f->len; ++i) {
        printf("%02X", f->data[i]);
    }
    printf("\n");
}

int main(void)
{
    rjs_can_frame_t f;

    rjs_heartbeat_t hb = {.state = RJS_NODE_STATE_OPERATIONAL, .error_flags = RJS_ERR_PEER_LOST,
                          .uptime_ms = 123456u, .fw_major = 0, .fw_minor = 1, .fw_patch = 0};
    rjs_encode_heartbeat(2, &hb, &f);
    print_frame(&f);

    rjs_command_t c = {.mode = RJS_MODE_POSITION, .target_mdeg = -90000, .max_velocity_mdeg_s = 90000,
                       .sequence = 77};
    rjs_encode_command(RJS_NODE_BROADCAST, &c, &f);
    print_frame(&f);

    rjs_joint_state_t js = {.mode = RJS_MODE_POSITION, .position_mdeg = 45500, .velocity_mdeg_s = -30000,
                            .current_ma = 80, .temperature_c10 = 251, .last_command_seq = 77};
    rjs_encode_joint_state(2, &js, &f);
    print_frame(&f);

    rjs_emergency_t e = {.code = 0x1001, .detail = 42};
    rjs_encode_emergency(3, &e, &f);
    print_frame(&f);
    return 0;
}
