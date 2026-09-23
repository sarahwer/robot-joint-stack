/*
 * joint.c - velocity/acceleration limited joint model with command timeout.
 *
 * SPDX-License-Identifier: MIT
 */
#include "rjs/joint.h"

#include <string.h>

static int32_t clamp32(int64_t v, int32_t lo, int32_t hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return (int32_t)v;
}

static int64_t abs64(int64_t v)
{
    return v < 0 ? -v : v;
}

/* Integer square root (bitwise method), exact floor(sqrt(x)). */
static uint64_t isqrt_u64(uint64_t x)
{
    uint64_t r   = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > x) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

void rjs_joint_init(rjs_joint_t *j, const rjs_joint_config_t *cfg)
{
    memset(j, 0, sizeof(*j));
    j->cfg  = *cfg;
    j->mode = RJS_MODE_DISABLED;
    if (j->cfg.accel_mdeg_s2 <= 0) {
        j->cfg.accel_mdeg_s2 = 1;
    }
}

void rjs_joint_command(rjs_joint_t *j, const rjs_command_t *cmd)
{
    j->last_seq         = cmd->sequence;
    j->since_command_ms = 0;
    j->mode             = cmd->mode;
    j->error_flags &= (uint16_t)~RJS_ERR_PEER_LOST;

    if (cmd->mode != RJS_MODE_POSITION) {
        return;
    }

    j->max_velocity_mdeg_s = cmd->max_velocity_mdeg_s;
    if (cmd->target_mdeg < j->cfg.min_mdeg || cmd->target_mdeg > j->cfg.max_mdeg) {
        j->error_flags |= RJS_ERR_LIMIT_REACHED;
    } else {
        j->error_flags &= (uint16_t)~RJS_ERR_LIMIT_REACHED;
    }
    j->target_mdeg = clamp32(cmd->target_mdeg, j->cfg.min_mdeg, j->cfg.max_mdeg);
}

void rjs_joint_step(rjs_joint_t *j, uint32_t dt_ms)
{
    if (dt_ms == 0u) {
        return;
    }

    /* Command timeout: a joint that loses its controller must stop. */
    if (j->mode == RJS_MODE_POSITION && j->cfg.command_timeout_ms != 0u) {
        j->since_command_ms += dt_ms;
        if (j->since_command_ms > j->cfg.command_timeout_ms) {
            j->mode = RJS_MODE_DISABLED;
            j->error_flags |= RJS_ERR_PEER_LOST;
            j->timeouts++;
        }
    }

    if (j->mode != RJS_MODE_POSITION) {
        /* Disabled: brake to standstill with the acceleration limit. */
        const int64_t dv = (int64_t)j->cfg.accel_mdeg_s2 * dt_ms / 1000;
        int64_t v = j->velocity_mdeg_s;
        if (abs64(v) <= dv) {
            v = 0;
        } else {
            v += (v > 0) ? -dv : dv;
        }
        j->velocity_mdeg_s = (int32_t)v;
        j->position_mdeg = clamp32((int64_t)j->position_mdeg + v * dt_ms / 1000, j->cfg.min_mdeg, j->cfg.max_mdeg);
        return;
    }

    const int64_t err = (int64_t)j->target_mdeg - j->position_mdeg;
    const int64_t a   = j->cfg.accel_mdeg_s2;

    /* Largest speed from which we can still stop at the target: v^2 = 2*a*d. */
    const int64_t v_stop = (int64_t)isqrt_u64((uint64_t)(2 * a * abs64(err)));

    int64_t v_limit = j->max_velocity_mdeg_s;
    if (v_stop < v_limit) {
        v_limit = v_stop;
    }
    const int64_t v_want = (err > 0) ? v_limit : -v_limit;

    /* Move velocity towards v_want, limited by acceleration. */
    int64_t dv_max = a * (int64_t)dt_ms / 1000;
    if (dv_max < 1) {
        dv_max = 1;
    }
    int64_t v = j->velocity_mdeg_s;
    if (v_want > v) {
        v = (v_want - v > dv_max) ? v + dv_max : v_want;
    } else {
        v = (v - v_want > dv_max) ? v - dv_max : v_want;
    }

    int64_t step = v * dt_ms / 1000;
    /* Never overshoot the target. */
    if ((err > 0 && step > err) || (err < 0 && step < err)) {
        step = err;
        v = 0;
    }
    /* Integer truncation can round a small step to zero: crawl the last mdeg. */
    if (step == 0 && err != 0) {
        step = (err > 0) ? 1 : -1;
    }
    if (step == err) {
        v = 0;
    }

    j->velocity_mdeg_s = (int32_t)v;
    j->position_mdeg   = clamp32((int64_t)j->position_mdeg + step, j->cfg.min_mdeg, j->cfg.max_mdeg);
}

void rjs_joint_get_state(const rjs_joint_t *j, rjs_joint_state_t *out)
{
    out->mode             = j->mode;
    out->position_mdeg    = j->position_mdeg;
    out->velocity_mdeg_s  = j->velocity_mdeg_s;
    /* Simple placeholder model until a real current sensor is connected. */
    int32_t v = j->velocity_mdeg_s < 0 ? -j->velocity_mdeg_s : j->velocity_mdeg_s;
    out->current_ma       = (int16_t)(j->mode == RJS_MODE_POSITION ? 50 + v / 1000 : 0);
    out->temperature_c10  = 250;
    out->last_command_seq = j->last_seq;
}
