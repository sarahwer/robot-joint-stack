/*
 * test_joint.c - motion limits, soft limits and command timeout of the joint model.
 *
 * SPDX-License-Identifier: MIT
 */
#include "rjs/joint.h"
#include "test_framework.h"

static const rjs_joint_config_t k_cfg = {
    .min_mdeg = -170000,
    .max_mdeg = 170000,
    .accel_mdeg_s2 = 200000,       /* 200 deg/s^2 */
    .command_timeout_ms = 500,
};

static rjs_command_t pos_cmd(int32_t target, int32_t vmax, uint16_t seq)
{
    rjs_command_t c = {.mode = RJS_MODE_POSITION, .target_mdeg = target,
                       .max_velocity_mdeg_s = vmax, .sequence = seq};
    return c;
}

static void test_reaches_target_without_overshoot(void)
{
    rjs_joint_t j;
    rjs_joint_init(&j, &k_cfg);
    rjs_command_t c = pos_cmd(90000, 60000, 1);
    int32_t max_pos = 0;
    int32_t max_vel = 0;
    for (int t = 0; t < 4000; ++t) {           /* 4 s at 1 ms */
        if (t % 100 == 0) {
            rjs_joint_command(&j, &c);          /* controller refreshes at 10 Hz */
        }
        rjs_joint_step(&j, 1);
        if (j.position_mdeg > max_pos) {
            max_pos = j.position_mdeg;
        }
        if (j.velocity_mdeg_s > max_vel) {
            max_vel = j.velocity_mdeg_s;
        }
    }
    CHECK_EQ(j.position_mdeg, 90000);
    CHECK_EQ(j.velocity_mdeg_s, 0);
    CHECK(max_pos <= 90000);                   /* no overshoot */
    CHECK(max_vel <= 60000);                   /* velocity limit respected */
    CHECK(max_vel >= 59000);                   /* and actually reached */
}

static void test_acceleration_limit(void)
{
    rjs_joint_t j;
    rjs_joint_init(&j, &k_cfg);
    rjs_command_t c = pos_cmd(170000, 1000000, 1);
    rjs_joint_command(&j, &c);
    int32_t prev_v = 0;
    for (int t = 0; t < 100; ++t) {
        rjs_joint_step(&j, 1);
        int32_t dv = j.velocity_mdeg_s - prev_v;
        CHECK(dv <= 200);                      /* 200 deg/s^2 * 1 ms = 200 mdeg/s */
        prev_v = j.velocity_mdeg_s;
    }
}

static void test_soft_limit_clamps_and_flags(void)
{
    rjs_joint_t j;
    rjs_joint_init(&j, &k_cfg);
    rjs_command_t c = pos_cmd(500000, 90000, 7);
    rjs_joint_command(&j, &c);
    CHECK_EQ(j.target_mdeg, 170000);
    CHECK(j.error_flags & RJS_ERR_LIMIT_REACHED);

    c = pos_cmd(10000, 90000, 8);
    rjs_joint_command(&j, &c);
    CHECK(!(j.error_flags & RJS_ERR_LIMIT_REACHED));
    CHECK_EQ(j.last_seq, 8);
}

static void test_command_timeout_stops_joint(void)
{
    rjs_joint_t j;
    rjs_joint_init(&j, &k_cfg);
    rjs_command_t c = pos_cmd(170000, 90000, 1);
    rjs_joint_command(&j, &c);
    for (int t = 0; t < 400; ++t) {
        rjs_joint_step(&j, 1);
    }
    CHECK_EQ(j.mode, RJS_MODE_POSITION);
    CHECK(j.velocity_mdeg_s > 0);

    for (int t = 0; t < 200; ++t) {            /* controller went silent */
        rjs_joint_step(&j, 1);
    }
    CHECK_EQ(j.mode, RJS_MODE_DISABLED);
    CHECK_EQ(j.timeouts, 1);
    CHECK(j.error_flags & RJS_ERR_PEER_LOST);

    for (int t = 0; t < 1000; ++t) {           /* brakes to standstill */
        rjs_joint_step(&j, 1);
    }
    CHECK_EQ(j.velocity_mdeg_s, 0);

    rjs_joint_command(&j, &c);                 /* new command clears the flag */
    CHECK(!(j.error_flags & RJS_ERR_PEER_LOST));
}

static void test_negative_direction_and_state(void)
{
    rjs_joint_t j;
    rjs_joint_init(&j, &k_cfg);
    rjs_command_t c = pos_cmd(-45000, 30000, 3);
    for (int t = 0; t < 3000; ++t) {
        if (t % 100 == 0) {
            rjs_joint_command(&j, &c);
        }
        rjs_joint_step(&j, 1);
        CHECK(j.position_mdeg >= -45000);
    }
    CHECK_EQ(j.position_mdeg, -45000);
    rjs_joint_state_t s;
    rjs_joint_get_state(&j, &s);
    CHECK_EQ(s.position_mdeg, -45000);
    CHECK_EQ(s.last_command_seq, 3);
    CHECK_EQ(s.mode, RJS_MODE_POSITION);
}

int main(void)
{
    RUN(test_reaches_target_without_overshoot);
    RUN(test_acceleration_limit);
    RUN(test_soft_limit_clamps_and_flags);
    RUN(test_command_timeout_stops_joint);
    RUN(test_negative_direction_and_state);
    TEST_MAIN_END();
}
