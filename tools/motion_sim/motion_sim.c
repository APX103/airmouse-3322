/* Host simulation of the AirMouse motion pipeline (gyro -> per-tick cursor delta).
 *
 * It compiles the REAL pipeline source (motion_slice.inc, extracted from sle_client.c by
 * extract.py) against a stub clock and a stub IMU, then feeds rotation profiles and checks
 * the pixel deltas it emits:
 *   - the cursor travels the right distance: sum(dx) = rate x gain x time;
 *   - it travels smoothly: slow motion gives a steady trickle of 1-px steps instead of
 *     nothing, and the step size never bursts;
 *   - a late loop iteration (the task wakes up after 40 ms instead of 10) covers the
 *     distance it should, instead of shrinking to a 10 ms step;
 *   - hand tremor below the still gate never moves the cursor.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ERRCODE_SUCC 0
#define AIM_IMU_DATA_REG 0x2D
#define IMU_LSB_PER_DPS 131.072 /* default gyro scale: +-250 dps */

static uint64_t g_now_us = 0;      /* the stub clock */
static double g_rate_dps[3] = {0}; /* what the stub IMU reports: gyro x, y, z in deg/s */

static uint64_t uapi_tcxo_get_us(void)
{
    return g_now_us;
}

static int aim_imu_read_regs(uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)reg;
    (void)len;
    for (int i = 0; i < 3; i++) {
        int16_t a = (i == 2) ? 16384 : 0; /* gravity on Z */
        int16_t g = (int16_t)lround(g_rate_dps[i] * IMU_LSB_PER_DPS);
        buf[i * 2] = (uint8_t)(a >> 8);
        buf[i * 2 + 1] = (uint8_t)a;
        buf[6 + i * 2] = (uint8_t)(g >> 8);
        buf[7 + i * 2] = (uint8_t)g;
    }
    buf[12] = 0;
    buf[13] = 0;
    return ERRCODE_SUCC;
}

static uint32_t g_aim_moved_px = 0;      /* defined above the extracted block in the firmware */
static volatile int g_aim_gain;           /* set to AIM_GAIN_DEFAULT in main(): the macro lives in the slice */

#include "motion_slice.inc"

/* ---------------- harness ---------------- */
static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            g_failures++; \
            printf("  FAIL: "); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

static const uint32_t k_tick_ms = 10;

static long g_sum_x;   /* cursor displacement since the counters were reset */
static long g_sum_y;
static int g_out_ticks; /* ticks that emitted a non-zero delta */
static int g_gap;       /* ticks since the last non-zero delta */
static int g_gap_max;   /* longest run of zero deltas */
static int g_step_max;  /* largest single-tick delta */
static long g_old_sum;  /* what the previous firmware emitted for the same input: fixed 10 ms, truncated */

static void counters_reset(void)
{
    g_sum_x = g_sum_y = 0;
    g_out_ticks = g_gap = g_gap_max = g_step_max = 0;
    g_old_sum = 0;
}

static void pipeline_reset(void)
{
    g_aim_gyro_lpf_init = false;
    g_aim_gyro_bias_rad[0] = g_aim_gyro_bias_rad[1] = g_aim_gyro_bias_rad[2] = 0;
    g_aim_px_acc_x = g_aim_px_acc_y = 0;
    counters_reset();
}

/* One loop iteration that wakes `dt_ms` after the previous one, with the board turning at
 * (dps_z, dps_x) deg/s. */
static void tick(uint32_t dt_ms, double dps_z, double dps_x)
{
    int16_t dx = 0;
    int16_t dy = 0;
    g_now_us += (uint64_t)dt_ms * 1000;
    g_rate_dps[0] = dps_x;
    g_rate_dps[1] = 0;
    g_rate_dps[2] = dps_z;
    if (!aim_ekf_step(&dx, &dy)) {
        printf("  FAIL: IMU read failed in the stub\n");
        g_failures++;
        return;
    }
    g_sum_x += dx;
    g_sum_y += dy;
    /* previous firmware: fixed 10 ms per tick whatever the real period, fractional pixels dropped */
    g_old_sum += (long)(-dps_z * AIM_RATE_GAIN * 0.01);
    if (dx != 0 || dy != 0) {
        g_out_ticks++;
        g_gap = 0;
        if (abs(dx) > g_step_max) {
            g_step_max = abs(dx);
        }
    } else {
        g_gap++;
        if (g_gap > g_gap_max) {
            g_gap_max = g_gap;
        }
    }
}

static void run(double dps_z, double dps_x, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        tick(k_tick_ms, dps_z, dps_x);
    }
}

static long ideal(double dps, double seconds)
{
    return lround(dps * AIM_RATE_GAIN * seconds);
}

#define NEAR(got, want) (labs((got) - (want)) <= 1)

int main(void)
{
    g_aim_gain = AIM_GAIN_DEFAULT;
    printf("config: gain=%d px/deg tick=%ums still gate=%.1f deg/s bias EMA=%.2f dt cap=%dms\n", AIM_GAIN_DEFAULT,
        k_tick_ms, AIM_STILL_DPS, AIM_BIAS_EMA, (int)(AIM_DT_MAX_S * 1000));

    /* 1. slow turn, 2 deg/s for 2 s: below the old dead zone (a 10 ms step was < 1 px) */
    pipeline_reset();
    run(2.0, 0, 200);
    printf("1. slow turn 2 deg/s x 2 s: cursor %ld px (ideal %ld; previous firmware %ld px); moved on %d of 200 "
           "ticks, longest pause %d tick(s), largest step %d px\n",
        -g_sum_x, ideal(2.0, 2), -g_old_sum, g_out_ticks, g_gap_max, g_step_max);
    CHECK(g_old_sum == 0, "reference: the previous firmware must show 0 px here, otherwise this test proves nothing");
    CHECK(NEAR(-g_sum_x, ideal(2.0, 2)), "slow turn distance %ld (want %ld +-1)", -g_sum_x, ideal(2.0, 2));
    CHECK(g_out_ticks >= 160, "slow turn emitted on %d/200 ticks (want >= 160: a steady trickle)", g_out_ticks);
    CHECK(g_gap_max <= 1, "slow turn longest pause %d ticks (want <= 1)", g_gap_max);
    CHECK(g_step_max == 1, "slow turn largest step %d px (want 1)", g_step_max);

    /* 2. ordinary turn, 30 deg/s for 1 s: every tick must move, step size steady */
    pipeline_reset();
    run(30.0, 0, 100);
    printf("2. turn 30 deg/s x 1 s: cursor %ld px (ideal %ld; previous firmware %ld px); moved on %d of 100 ticks, "
           "largest step %d px\n",
        -g_sum_x, ideal(30.0, 1), -g_old_sum, g_out_ticks, g_step_max);
    CHECK(NEAR(-g_sum_x, ideal(30.0, 1)), "distance %ld (want %ld +-1)", -g_sum_x, ideal(30.0, 1));
    CHECK(g_out_ticks == 100, "ticks that moved the cursor %d (want 100)", g_out_ticks);
    CHECK(g_step_max <= 14, "largest step %d px (want <= 14: about 12.6 px/tick)", g_step_max);

    /* 3. the loop wakes late: periods 10,10,40,10,10,25 ms. The distance must not shrink. */
    pipeline_reset();
    const uint32_t late[6] = { 10, 10, 40, 10, 10, 25 };
    double seconds = 0;
    for (int i = 0; i < 600; i++) {
        tick(late[i % 6], 30.0, 0);
        seconds += late[i % 6] / 1000.0;
    }
    printf("3. late loop (10/10/40/10/10/25 ms) at 30 deg/s for %.2f s: cursor %ld px (ideal %ld; previous firmware "
           "%ld px); ticks without motion %d, largest step %d px\n",
        seconds, -g_sum_x, ideal(30.0, seconds), -g_old_sum, 600 - g_out_ticks, g_step_max);
    CHECK(-g_old_sum < ideal(30.0, seconds) * 6 / 10,
          "reference: the previous firmware must lose a large part of the distance here (it emitted %ld)", -g_old_sum);
    CHECK(NEAR(-g_sum_x, ideal(30.0, seconds)), "late loop distance %ld (want %ld +-1)", -g_sum_x, ideal(30.0, seconds));
    CHECK(g_out_ticks == 600, "late loop: %d ticks without motion (want 0)", 600 - g_out_ticks);
    CHECK(g_step_max >= 50 && g_step_max <= 51, "late loop largest step %d px (want 50..51 = the 40 ms tick)",
          g_step_max);

    /* 4. a long stall (500 ms) in the middle of a turn must not become a jump: dt is capped */
    pipeline_reset();
    run(30.0, 0, 100);
    counters_reset();
    tick(500, 30.0, 0);
    printf("4. 500 ms stall during a 30 deg/s turn: that tick moves %d px (cap = rate x gain x %d ms = %ld px)\n",
        g_step_max, (int)(AIM_DT_MAX_S * 1000), lround(30.0 * AIM_RATE_GAIN * AIM_DT_MAX_S));
    CHECK(g_step_max >= 63 && g_step_max <= 64, "stall tick %d px (want 63..64)", g_step_max);

    /* 5. Y axis: rolling about X moves the cursor with the same math */
    pipeline_reset();
    run(0, -30.0, 100);
    printf("5. roll -30 deg/s x 1 s: cursor dy %ld px (ideal %ld)\n", g_sum_y, ideal(-30.0, 1));
    CHECK(NEAR(g_sum_y, ideal(-30.0, 1)) && g_sum_x == 0, "roll distance %ld (want %ld +-1, dx 0)", g_sum_y,
          ideal(-30.0, 1));

    /* 6. hand tremor: 0.7 deg/s must never move the cursor, not even in the first 0.3 s
     *    before the still gate closes (sub-pixel accumulation would turn it into visible drift) */
    pipeline_reset();
    unsigned int rng = 7;
    for (int i = 0; i < 1000; i++) {
        rng = rng * 1103515245u + 12345u;
        double n = ((int)(rng >> 16) % 2001 - 1000) / 1000.0; /* -1.0 .. 1.0 */
        tick(k_tick_ms, 0.5 * n, 0.5 * (1.0 - n * n));
    }
    printf("6. tremor up to 0.7 deg/s for 10 s: cursor moved %ld px\n", g_sum_x + g_sum_y);
    CHECK(g_sum_x == 0 && g_sum_y == 0, "tremor moved the cursor %ld px (want 0)", g_sum_x + g_sum_y);

    /* 7. stop, then start again: the cursor moves on the very first tick, and the whole stroke
     *    covers the right distance (the light filter lags a fraction of a tick and gives it back at the end) */
    pipeline_reset();
    run(30.0, 0, 100);
    run(0, 0, 500); /* at rest for 5 s */
    counters_reset();
    tick(k_tick_ms, 30.0, 0);
    {
        int first = g_step_max;
        run(30.0, 0, 3);
        run(0, 0, 3);
        printf("7. after 5 s at rest, four ticks at 30 deg/s then rest: first tick %d px, whole stroke %ld px "
               "(ideal %ld)\n",
            first, -g_sum_x, ideal(30.0, 0.04));
        CHECK(first >= 10, "first tick after rest %d px (want >= 10: no dead time)", first);
        CHECK(NEAR(-g_sum_x, ideal(30.0, 0.04)), "stroke distance %ld (want %ld +-1)", -g_sum_x, ideal(30.0, 0.04));
    }

    printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
