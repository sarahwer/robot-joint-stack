/*
 * test_can_timing.c - bit timing results for the clocks used on our boards.
 *
 * SPDX-License-Identifier: MIT
 */
#include "rjs/can_timing.h"
#include "test_framework.h"

/* STM32 FDCAN register limits (nominal and data phase). */
static const rjs_can_timing_limits_t k_nominal = {512, 256, 128, 128};
static const rjs_can_timing_limits_t k_data    = {32, 32, 16, 16};

static void check_bitrate(uint32_t clk, uint32_t br, const rjs_can_timing_t *t)
{
    CHECK_EQ((uint64_t)clk / ((uint64_t)t->prescaler * (1u + t->tseg1 + t->tseg2)), br);
}

static void test_200mhz_nominal_1mbit(void)
{
    rjs_can_timing_t t;
    CHECK(rjs_can_calc_timing(200000000u, 1000000u, 800, &k_nominal, &t));
    CHECK_EQ(t.prescaler, 1);
    CHECK_EQ(t.tseg1, 159);
    CHECK_EQ(t.tseg2, 40);
    CHECK_EQ(t.sjw, 40);
    CHECK_EQ(t.sample_point_permille, 800);
    check_bitrate(200000000u, 1000000u, &t);
}

static void test_200mhz_data_2mbit_matches_st_example(void)
{
    /* ST's FDCAN_Com_IT example uses prescaler 4, tseg1 19, tseg2 5 here. */
    rjs_can_timing_t t;
    CHECK(rjs_can_calc_timing(200000000u, 2000000u, 800, &k_data, &t));
    CHECK_EQ(t.prescaler, 4);
    CHECK_EQ(t.tseg1, 19);
    CHECK_EQ(t.tseg2, 5);
    check_bitrate(200000000u, 2000000u, &t);
}

static void test_200mhz_data_5mbit(void)
{
    rjs_can_timing_t t;
    CHECK(rjs_can_calc_timing(200000000u, 5000000u, 750, &k_data, &t));
    check_bitrate(200000000u, 5000000u, &t);
    CHECK(t.tseg1 <= 32 && t.tseg2 <= 16);
}

static void test_24mhz_hse(void)
{
    rjs_can_timing_t t;
    CHECK(rjs_can_calc_timing(24000000u, 500000u, 875, &k_nominal, &t));
    check_bitrate(24000000u, 500000u, &t);
    CHECK(t.sample_point_permille >= 850 && t.sample_point_permille <= 900);
}

static void test_impossible_requests(void)
{
    rjs_can_timing_t t;
    CHECK(!rjs_can_calc_timing(24000000u, 7000000u, 800, &k_data, &t));   /* not exact */
    CHECK(!rjs_can_calc_timing(200000000u, 0u, 800, &k_data, &t));
    CHECK(!rjs_can_calc_timing(200000000u, 1000000u, 300, &k_data, &t));  /* silly sample point */
    CHECK(!rjs_can_calc_timing(200000000u, 1000000u, 800, NULL, &t));
}

int main(void)
{
    RUN(test_200mhz_nominal_1mbit);
    RUN(test_200mhz_data_2mbit_matches_st_example);
    RUN(test_200mhz_data_5mbit);
    RUN(test_24mhz_hse);
    RUN(test_impossible_requests);
    TEST_MAIN_END();
}
