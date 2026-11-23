/* Host simulation of the AirMouse rotary-encoder decoder.
 *
 * It compiles the REAL decoder source (enc_slice.inc, extracted from sle_client.c by
 * extract.py) against stub GPIO, then replays encoder waveforms and checks the resulting
 * gain changes.
 *
 * Phase state s = (A closed << 1) | B closed, "closed" = pin level differs from the pin's
 * open-circuit level (g_aim_enc_open_mask, sampled by the firmware at power-on).
 *   "+" direction (B leads A): 00 -> 01 -> 11 -> 10 -> 00
 *   "-" direction (A leads B): 00 -> 10 -> 11 -> 01 -> 00
 *   detent rest state: both contacts open = 00
 * Both wirings are exercised: contacts to GND (idle high, needs external pull-ups) and
 * contacts to 3.3V (idle low, no external parts) -- see g_active_high.
 *
 * Two sampling modes are exercised, both through the decoder code that ships in the firmware:
 *   poll 1ms : aim_enc_feed() once per 1 ms tick (dt = 1).
 *   edge-irq : what the firmware does with AIM_ENC_IRQ=1. The pins are sampled only when a
 *              contact changes (ISR -> semaphore -> task) and when the timeout returned by
 *              aim_enc_service() expires; virtual time stands still in between.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int osal_task;
typedef int osal_semaphore;
typedef int pin_t;
enum { S_MGPIO12 = 44, S_MGPIO13 = 45, S_MGPIO14 = 46 };
typedef enum { GPIO_LEVEL_LOW = 0, GPIO_LEVEL_HIGH = 1 } gpio_level_t;
static int g_pin_level[128]; /* simulated pin levels, 1 = high */
static int g_active_high = 0; /* 0: contacts close to GND (open = high) ; 1: contacts close to 3.3V (open = low) */
static int g_event_mode = 0; /* 0: poll every 1 ms ; 1: edge interrupts + timeouts */

static gpio_level_t uapi_gpio_get_val(int pin)
{
    return (gpio_level_t)g_pin_level[pin];
}

static void osal_sem_up(osal_semaphore *sem)
{
    (void)sem;
}

#include "enc_slice.inc"

/* ---------------- harness ---------------- */
static int g_failures = 0;
static int g_checks = 0;
static int g_events = 0;  /* signed detent events seen since the counters were reset */
static int g_resets = 0;  /* confirmed SW presses */
static long g_samples = 0; /* edge-irq mode: task wake-ups (edge + timeout) since the counters were reset */

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

/* Drive the pin levels for a contact state: a closed contact sits at the active level. */
static void pins(int state, int sw_closed)
{
    int active = g_active_high ? 1 : 0;
    g_pin_level[AIM_ENC_A_PIN] = ((state >> 1) & 1) ? active : !active;
    g_pin_level[AIM_ENC_B_PIN] = (state & 1) ? active : !active;
    g_pin_level[AIM_ENC_SW_PIN] = sw_closed ? active : !active;
}

static void counters_reset(void)
{
    g_events = 0;
    g_resets = 0;
    g_samples = 0;
}

/* edge-irq mode: virtual time and the one pending timeout of the firmware's loop */
static uint32_t g_now;
static uint32_t g_deadline;
static int g_have_deadline;
static int g_last_raw; /* pin levels as of the last sample: a change is what raises the interrupt */

/* One decoder update on the current pin levels, plus bookkeeping. */
static void sample(void)
{
    int gain_before = g_aim_gain;
    uint8_t sw_before = g_aim_enc_sw_last;
    if (g_event_mode) {
        g_aim_enc_timeout_ms = aim_enc_service(g_now); /* the firmware's loop body after waking up */
        g_samples++;
    } else {
        aim_enc_feed(aim_enc_read(), 1);
    }
    if (g_aim_enc_sw_last > sw_before) {
        g_resets++;
    }
    if (g_aim_gain != gain_before && g_aim_enc_sw_last == sw_before) {
        g_events += (g_aim_gain - gain_before) / AIM_ENC_GAIN_STEP;
    }
}

/* edge-irq mode: let virtual time run to `to`, waking the task at each pending timeout */
static void ev_run_until(uint32_t to)
{
    while (g_have_deadline && (int32_t)(g_deadline - to) <= 0) {
        g_now = g_deadline;
        sample();
        g_have_deadline = (g_aim_enc_timeout_ms != 0);
        g_deadline = g_now + g_aim_enc_timeout_ms;
    }
    g_now = to;
}

static void tick(int state, int sw_closed)
{
    pins(state, sw_closed);
    if (!g_event_mode) {
        sample();
        return;
    }
    if (aim_enc_read() == g_last_raw) {
        return; /* no edge, no interrupt, the task keeps sleeping */
    }
    g_last_raw = aim_enc_read();
    aim_enc_isr(0, 0); /* edge -> ISR posts the semaphore -> the task runs now (before any timeout) */
    sample();
    g_have_deadline = (g_aim_enc_timeout_ms != 0);
    g_deadline = g_now + g_aim_enc_timeout_ms;
}

static void hold(int ms, int state, int sw_closed)
{
    if (g_event_mode) {
        tick(state, sw_closed);
        ev_run_until(g_now + (uint32_t)ms);
        return;
    }
    for (int i = 0; i < ms; i++) {
        tick(state, sw_closed);
    }
}

static const int k_plus[5] = { 0, 1, 3, 2, 0 };  /* one detent, B leads */
static const int k_minus[5] = { 0, 2, 3, 1, 0 }; /* one detent, A leads */

/* One clean detent: each phase held `dwell` ms, then `rest_ms` at the detent. */
static void detent(int dir, int dwell, int rest_ms)
{
    const int *seq = (dir > 0) ? k_plus : k_minus;
    for (int i = 1; i <= 4; i++) {
        hold(dwell, seq[i], 0);
    }
    hold(rest_ms, 0, 0);
}

static unsigned int g_rng = 12345u;
static unsigned int rnd(unsigned int n) /* deterministic LCG, 0..n-1 */
{
    g_rng = g_rng * 1103515245u + 12345u;
    return (g_rng >> 16) % n;
}

/* One detent the way a real EC11 behaves: every contact make/break chatters between the
 * old and the new phase for `bounce_ms` (random 1..3 tick runs), then the new phase
 * settles for `dwell` ms. Lands on the detent and rests `rest_ms`. */
static void detent_bouncy(int dir, int dwell, int bounce_ms, int rest_ms)
{
    const int *seq = (dir > 0) ? k_plus : k_minus;
    for (int i = 1; i <= 4; i++) {
        int left = bounce_ms;
        int cur = seq[i - 1];
        while (left > 0) {
            int run = 1 + (int)rnd(3);
            if (run > left) {
                run = left;
            }
            hold(run, cur, 0);
            left -= run;
            cur = (cur == seq[i - 1]) ? seq[i] : seq[i - 1];
        }
        hold(dwell, seq[i], 0);
    }
    hold(rest_ms, 0, 0);
}

static void decoder_reset(void)
{
    /* what the firmware samples at power-on: every contact open at its idle level */
    g_aim_enc_open_mask = g_active_high ? 0x00 : 0x07;
    g_aim_enc_stable = 0;
    g_aim_enc_acc = 0;
    g_aim_enc_rest_ms = 0;
    g_aim_enc_sw_last = 0;
    g_aim_enc_sw_cand = 0;
    g_aim_enc_sw_ms = 0;
    g_aim_gain = AIM_GAIN_DEFAULT;
    counters_reset();
    if (g_event_mode) {
        g_now = 70000; /* away from 0: nothing may depend on time starting at boot */
        g_have_deadline = 0;
        g_aim_enc_timeout_ms = 0;
        pins(0, 0);
        g_last_raw = aim_enc_read();
        sample(); /* the firmware's first sample right after init */
        g_have_deadline = (g_aim_enc_timeout_ms != 0);
        g_deadline = g_now + g_aim_enc_timeout_ms;
        counters_reset(); /* the first sample is not an event */
    }
}

/* The whole scenario list, for one wiring and one sampling mode. Returns non-zero on any failure. */
static int run_scenarios(void)
{
    g_checks = 0;
    g_failures = 0;
    printf("\n===== wiring: contacts close to %s | sampling: %s =====\n",
        g_active_high ? "3.3V (open = low, no external pull-ups)" : "GND (open = high, external pull-ups)",
        g_event_mode ? "edge-irq (wake on A/B/SW edges + timeouts)" : "poll 1ms");
    (void)g_aim_enc_task; /* declared by the slice, only the firmware task uses it */
    printf("config: REST_MS=%d STEPS_PER_DETENT=%d DIR_SIGN=%+d STEP=%d range=[%d,%d] default=%d SW_DEBOUNCE=%dms\n",
        AIM_ENC_REST_MS, AIM_ENC_STEPS_PER_DETENT, AIM_ENC_DIR_SIGN, AIM_ENC_GAIN_STEP, AIM_GAIN_MIN, AIM_GAIN_MAX,
        AIM_GAIN_DEFAULT, AIM_ENC_SW_DEBOUNCE_MS);

    /* 0. edge-irq only: the task must not wake while nothing moves, however long that takes,
     *    and must wake exactly once per edge when something does */
    if (g_event_mode) {
        decoder_reset();
        hold(600000, 0, 0); /* ten minutes of silence */
        CHECK(g_events == 0 && g_resets == 0, "long idle: events=%d resets=%d (want 0/0)", g_events, g_resets);
        CHECK(g_samples == 0, "long idle: %ld task wake-ups (want 0)", g_samples);
        counters_reset();
        detent(+1, 10, 150);
        CHECK(g_events == +1 && g_aim_gain == AIM_GAIN_DEFAULT + 2, "detent after long idle: events=%d gain=%d (want +1/%d)",
              g_events, g_aim_gain, AIM_GAIN_DEFAULT + 2);
        CHECK(g_samples == 4, "one clean detent: %ld task wake-ups (want 4, one per edge)", g_samples);
        counters_reset();
        for (int i = 0; i < 6; i++) {
            tick(0, 1);
            tick(0, 0);
        }
        hold(100, 0, 1);
        hold(50, 0, 0);
        CHECK(g_resets == 1, "sw press w/ bounce: resets=%d (want 1)", g_resets);
        /* 12 bounce edges + the last press edge + its debounce timeout + the release edge
         * + its debounce timeout = 16 */
        CHECK(g_samples == 16, "sw press w/ bounce: %ld task wake-ups (want 16)", g_samples);
    }

    /* 1. idle at the detent: nothing must happen */
    decoder_reset();
    hold(1000, 0, 0);
    CHECK(g_events == 0 && g_resets == 0, "idle: events=%d resets=%d (want 0/0)", g_events, g_resets);

    /* 2. one detent each way, normal hand speed */
    decoder_reset();
    detent(+1, 10, 150);
    CHECK(g_events == +1 && g_aim_gain == AIM_GAIN_DEFAULT + 2, "one detent up: events=%d gain=%d (want +1/%d)", g_events,
              g_aim_gain, AIM_GAIN_DEFAULT + 2);
    counters_reset();
    detent(-1, 10, 150);
    CHECK(g_events == -1 && g_aim_gain == AIM_GAIN_DEFAULT, "one detent down: events=%d gain=%d (want -1/%d)", g_events,
              g_aim_gain, AIM_GAIN_DEFAULT);

    /* 3. three detents up in one continuous turn (no rest between) */
    decoder_reset();
    for (int d = 0; d < 3; d++) {
        detent(+1, 4, 0);
    }
    hold(150, 0, 0);
    CHECK(g_events == +3 && g_aim_gain == AIM_GAIN_DEFAULT + 6, "3 detents continuous: events=%d gain=%d (want +3/%d)",
              g_events, g_aim_gain, AIM_GAIN_DEFAULT + 6);

    /* 4. 20 detents at the sampling limit of the 1ms poll: a new phase every tick.
     *    (edge-irq has no such limit: every phase change is an interrupt) */
    decoder_reset();
    for (int d = 0; d < 20; d++) {
        detent(+1, 1, 0);
    }
    hold(150, 0, 0);
    CHECK(g_events == +20, "20 detents at 1ms/phase: events=%d (want +20)", g_events);

    /* 5. contact bounce around the detent (1-tick and 2-tick glitches) */
    decoder_reset();
    for (int i = 0; i < 40; i++) {
        tick(0, 0);
        tick(1, 0);
    }
    CHECK(g_events == 0, "bounce 1-tick: events=%d (want 0)", g_events);
    for (int i = 0; i < 40; i++) {
        hold(2, 1, 0);
        hold(2, 0, 0);
    }
    hold(150, 0, 0);
    CHECK(g_events == 0, "bounce 2-tick: events=%d (want 0)", g_events);

    /* 6. half a detent, hold, come back: no event */
    decoder_reset();
    hold(10, 1, 0);
    hold(300, 3, 0);
    hold(10, 1, 0);
    hold(200, 0, 0);
    CHECK(g_events == 0, "half detent and return: events=%d (want 0)", g_events);

    /* 7. a phase shorter than the sampling period (missed): that detent is lost, never
     *    invented, and the next detent is correct again (residue cleared at rest) */
    decoder_reset();
    hold(10, 3, 0); /* 00 -> 11 seen as a two-bit jump (01 was missed) */
    hold(10, 2, 0);
    hold(200, 0, 0);
    CHECK(g_events == 0, "missed step: events=%d (want 0, detent undercounted)", g_events);
    counters_reset();
    detent(+1, 10, 150);
    CHECK(g_events == +1, "detent after a missed step: events=%d (want +1)", g_events);

    /* 8. SW press with contact bounce: exactly one reset; a short glitch is ignored */
    decoder_reset();
    detent(+1, 10, 150);
    counters_reset();
    for (int i = 0; i < 6; i++) {
        tick(0, 1);
        tick(0, 0);
    }
    hold(100, 0, 1);
    for (int i = 0; i < 6; i++) {
        tick(0, 0);
        tick(0, 1);
    }
    hold(50, 0, 0);
    CHECK(g_resets == 1 && g_aim_gain == AIM_GAIN_DEFAULT, "sw press w/ bounce: resets=%d gain=%d (want 1/%d)", g_resets,
              g_aim_gain, AIM_GAIN_DEFAULT);
    counters_reset();
    hold(15, 0, 1); /* shorter than the debounce window */
    hold(50, 0, 0);
    CHECK(g_resets == 0, "sw glitch 15ms: resets=%d (want 0)", g_resets);

    /* 9. clamping at both ends */
    decoder_reset();
    for (int d = 0; d < 60; d++) {
        detent(+1, 10, 0);
    }
    hold(150, 0, 0);
    CHECK(g_events == (AIM_GAIN_MAX - AIM_GAIN_DEFAULT) / AIM_ENC_GAIN_STEP && g_aim_gain == AIM_GAIN_MAX,
          "clamp max: events=%d gain=%d (want %d/%d)", g_events, g_aim_gain,
          (AIM_GAIN_MAX - AIM_GAIN_DEFAULT) / AIM_ENC_GAIN_STEP, AIM_GAIN_MAX);
    counters_reset();
    for (int d = 0; d < 80; d++) {
        detent(-1, 10, 0);
    }
    hold(150, 0, 0);
    CHECK(g_events == -70 && g_aim_gain == AIM_GAIN_MIN, "clamp min: events=%d gain=%d (want -70/%d)", g_events,
          g_aim_gain, AIM_GAIN_MIN);

    /* 10. power-on while resting off the detent, then finish the stroke: only the 2 steps
     *     that really happened after boot are seen -> half a detent, no event */
    decoder_reset();
    hold(50, 3, 0);
    hold(10, 2, 0);
    hold(200, 0, 0);
    CHECK(g_events == 0, "boot off-detent then finish: events=%d (want 0)", g_events);

    /* 11. random stress, clean phases: N detents, random direction and speed */
    for (int round = 0; round < 10; round++) {
        decoder_reset();
        int expected = 0;
        int net = 0;
        for (int d = 0; d < 300; d++) {
            int dir = (int)rnd(2) * 2 - 1;
            int gain_now = AIM_GAIN_DEFAULT + net; /* net is an offset; the limits are absolute gain */
            if (gain_now + 2 * dir > AIM_GAIN_MAX || gain_now + 2 * dir < AIM_GAIN_MIN) {
                dir = -dir; /* stay inside the range so every detent must register */
            }
            detent(dir, 1 + (int)rnd(13), 30 + (int)rnd(200));
            expected += dir;
            net += 2 * dir;
        }
        CHECK(g_events == expected && g_aim_gain == AIM_GAIN_DEFAULT + net,
              "random clean round %d: events=%d expected=%d gain=%d (want %d)", round, g_events, expected, g_aim_gain,
              AIM_GAIN_DEFAULT + net);
    }

    /* 12. random stress, bouncing contacts: the same, but every transition chatters */
    const char *dbg = getenv("DBG"); /* DBG=<round> prints the detents of that round which did not register */
    for (int round = 0; round < 10; round++) {
        decoder_reset();
        int expected = 0;
        int net = 0;
        int total = 0;
        int lost = 0;
        for (int d = 0; d < 300; d++) {
            int dir = (int)rnd(2) * 2 - 1;
            int gain_now = AIM_GAIN_DEFAULT + net; /* net is an offset; the limits are absolute gain */
            if (gain_now + 2 * dir > AIM_GAIN_MAX || gain_now + 2 * dir < AIM_GAIN_MIN) {
                dir = -dir;
            }
            /* parameters first, in a fixed order, so the waveform sequence is reproducible */
            int dwell = 1 + (int)rnd(12);
            int bounce = (int)rnd(6);
            int rest = 120 + (int)rnd(100);
            counters_reset();
            detent_bouncy(dir, dwell, bounce, rest);
            total += g_events;
            if (g_events != dir) {
                lost++;
                if (dbg && atoi(dbg) == round) {
                    printf("  lost detent %3d: dir=%+d dwell=%2d bounce=%d rest=%3d events=%+d acc=%d stable=%u rest_ms=%u | gain=%d harness_net=%d\n",
                           d, dir, dwell, bounce, rest, g_events, g_aim_enc_acc, g_aim_enc_stable, g_aim_enc_rest_ms, g_aim_gain, net);
                }
            }
            expected += dir;
            net += 2 * dir;
        }
        CHECK(total == expected && g_aim_gain == AIM_GAIN_DEFAULT + net,
              "random bouncy round %d: events=%d expected=%d gain=%d (want %d) lost=%d", round, total, expected,
              g_aim_gain, AIM_GAIN_DEFAULT + net, lost);
    }

    printf("%s (%s wiring, %s): %d checks, %d failures\n", g_failures ? "FAILED" : "PASSED",
        g_active_high ? "3.3V" : "GND", g_event_mode ? "edge-irq" : "poll 1ms", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

/* One wiring: both sampling modes. Returns the number of failures. */
static int run_all(int active_high)
{
    int failed = 0;
    int total_failures = 0;
    int total_checks = 0;
    g_active_high = active_high;
    for (int mode = 0; mode <= 1; mode++) {
        g_event_mode = mode;
        failed |= run_scenarios();
        total_failures += g_failures;
        total_checks += g_checks;
    }
    g_failures = total_failures;
    g_checks = total_checks;
    return failed;
}

int main(void)
{
    int failed = run_all(0);
    int total_failures = g_failures;
    int total_checks = g_checks;
    failed |= run_all(1);
    total_failures += g_failures;
    total_checks += g_checks;
    printf("%s: %d checks, %d failures\n", total_failures ? "FAILED" : "PASSED", total_checks, total_failures);
    return failed;
}
