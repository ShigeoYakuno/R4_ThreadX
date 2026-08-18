#include "pulse_out.h"
#include "hal_data.h"
#include "uart.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/* TEMPORARY: was used to bisect the pulse-hang bug back when every pulse command re-opened GPT6
 * (R_GPT_Close()+R_GPT_Open(), to change its clock divider). That reconfigure path turned out to
 * be unreliable no matter how much settling delay (busy-wait or tx_thread_sleep()) was added
 * around it, so the design was changed instead: GPT6 is now opened exactly once, at boot, with a
 * fixed divider wide enough for the whole supported range, and period changes go through
 * R_GPT_PeriodSet() only -- see the comment above g_pulse_cfg below. Kept disabled (#if 0); flip
 * to re-enable if a similar issue ever needs bisecting again. */
static void pulse_dbg(const char * fmt, ...)
{
    (void) fmt;
#if 0
    char    msg[80];
    va_list args;

    va_start(args, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (n > 0)
    {
        size_t len = (size_t) n;
        if (len >= sizeof(msg))
        {
            len = sizeof(msg) - 1;
        }
        uart_write_blocking((const uint8_t *) msg, len);
    }
#endif
}

#define PULSE_OUT_CHANNEL      6
#define PULSE_OUT_PIN          BSP_IO_PORT_04_PIN_10 /* D12 = GTIOC6B */
#define PULSE_OUT_GPT_CLOCK_HZ 48000000UL             /* PCLKD; see bsp_clock_cfg.h (HOCO 48MHz, /1) */
#define PULSE_OUT_MIN_COUNTS   2U                      /* GTPR must be >=1, so period_counts must be >=2 */
#define PULSE_OUT_MAX_COUNTS   65535U                  /* GPT6 is a 16-bit counter on this MCU */

/* Fixed source divider: PCLKD/16 = 3MHz. Chosen so the *entire* documented range
 * [PULSE_OUT_PERIOD_NS_MIN, PULSE_OUT_PERIOD_NS_MAX] = [1us, 10ms] fits inside the 16-bit counter
 * with a single, never-changing divider:
 *   1us   -> 3 counts  (>= PULSE_OUT_MIN_COUNTS)
 *   10ms  -> 30000 counts (<= PULSE_OUT_MAX_COUNTS, well within the 65535 limit)
 * Because 3MHz*1us and 3MHz*1ms are both exact integers, every whole-us/ms period the UART
 * command parser can produce converts to period_counts with zero rounding error -- unlike the
 * earlier design (which picked the finest of several dividers per call and had to re-open GPT6
 * to change it), a fixed divider means R_GPT_Open() only ever runs once, in pulse_out_init().
 * Reconfiguring GPT6's divider via repeated R_GPT_Close()/R_GPT_Open() calls was empirically
 * unreliable (see docs/architecture.md) no matter how much settling delay was added around it, so
 * that reconfigure path is avoided entirely now: period changes only ever call R_GPT_PeriodSet(),
 * which is documented as safe to call whether the timer is running or stopped. */
//#define PULSE_OUT_SOURCE_DIV       TIMER_SOURCE_DIV_16
//#define PULSE_OUT_SOURCE_DIV_VALUE 16UL
#define PULSE_OUT_SOURCE_DIV       TIMER_SOURCE_DIV_1
#define PULSE_OUT_SOURCE_DIV_VALUE 1UL

static gpt_instance_ctrl_t g_pulse_ctrl;
static bool                g_pulse_opened; /* true once pulse_out_init() has opened GPT6 */
static bool                g_pulse_running;

/* GTIOCB drives D12; GTIOCA is unused. TIMER_MODE_PERIODIC forces a fixed 50% duty automatically
 * (see r_gpt.c), so duty is not configured here. */
static const gpt_extended_cfg_t g_pulse_cfg_extend = {
    .gtioca                = { .output_enabled = false, .stop_level = GPT_PIN_LEVEL_LOW },
    .gtiocb                = { .output_enabled = true,   .stop_level = GPT_PIN_LEVEL_LOW },
    .start_source          = GPT_SOURCE_NONE,
    .stop_source           = GPT_SOURCE_NONE,
    .clear_source          = GPT_SOURCE_NONE,
    .capture_a_source      = GPT_SOURCE_NONE,
    .capture_b_source      = GPT_SOURCE_NONE,
    .count_up_source       = GPT_SOURCE_NONE,
    .count_down_source     = GPT_SOURCE_NONE,
    .capture_filter_gtioca = GPT_CAPTURE_FILTER_NONE,
    .capture_filter_gtiocb = GPT_CAPTURE_FILTER_NONE,
    .capture_a_ipl         = BSP_IRQ_DISABLED,
    .capture_b_ipl         = BSP_IRQ_DISABLED,
    .capture_a_irq         = FSP_INVALID_VECTOR,
    .capture_b_irq         = FSP_INVALID_VECTOR,
    .p_pwm_cfg             = NULL,
    .gtior_setting         = { .gtior = 0 },
};

void pulse_out_init(void)
{
    /* D12 is not in the board's default pin table (g_bsp_pin_cfg), same situation as D1 in
     * uart.c -- configure it here for GPT's alternate function. */
    R_BSP_PinCfg(PULSE_OUT_PIN, IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_GPT1);

    /* R_GPT_Open() itself is deliberately NOT called here -- see pulse_out_set_period_ns(). */
}

bool pulse_out_set_period_ns(uint32_t period_ns)
{
    pulse_dbg("dbg: begin period_ns=%lu\r\n", (unsigned long) period_ns);

    if ((period_ns < PULSE_OUT_PERIOD_NS_MIN) || (period_ns > PULSE_OUT_PERIOD_NS_MAX))
    {
        pulse_dbg("dbg: out of range\r\n");
        return false;
    }

    /* period_counts = period_ns * (PULSE_OUT_GPT_CLOCK_HZ / PULSE_OUT_SOURCE_DIV_VALUE) / 1e9.
     * 64-bit math avoids overflow (period_ns up to 1e7 times a 48e6 clock doesn't fit in 32
     * bits); exact (no rounding) for every whole-us/ms period in the documented range, see the
     * comment above PULSE_OUT_SOURCE_DIV. */
    uint64_t counts64 = ((uint64_t) period_ns * PULSE_OUT_GPT_CLOCK_HZ)
                         / ((uint64_t) PULSE_OUT_SOURCE_DIV_VALUE * 1000000000ULL);

    if ((counts64 < PULSE_OUT_MIN_COUNTS) || (counts64 > PULSE_OUT_MAX_COUNTS))
    {
        pulse_dbg("dbg: counts out of range\r\n");
        return false; /* shouldn't happen within the documented ns range, but guard anyway */
    }

    uint32_t period_counts = (uint32_t) counts64;

    pulse_dbg("dbg: counts=%lu\r\n", (unsigned long) period_counts);

    if (!g_pulse_opened)
    {
        /* First-ever activation: R_GPT_Open() with the REAL requested period baked in, exactly
         * like the old per-call design did (which always produced a correct waveform -- the bug
         * there was calling Open() again on every later change, not Open() itself). This is the
         * only place R_GPT_Open() is ever called, so that reconfigure path never recurs. */
        const timer_cfg_t cfg = {
            .mode              = TIMER_MODE_PERIODIC,
            .period_counts     = period_counts,
            .source_div        = PULSE_OUT_SOURCE_DIV,
            .duty_cycle_counts = 0, /* unused: TIMER_MODE_PERIODIC always drives a 50% duty */
            .channel           = PULSE_OUT_CHANNEL,
            .cycle_end_ipl     = BSP_IRQ_DISABLED,
            .cycle_end_irq     = FSP_INVALID_VECTOR,
            .p_callback        = NULL,
            .p_context         = NULL,
            .p_extend          = &g_pulse_cfg_extend,
        };

        pulse_dbg("dbg: first-time open\r\n");
        if (FSP_SUCCESS != R_GPT_Open(&g_pulse_ctrl, &cfg))
        {
            pulse_dbg("dbg: open failed\r\n");
            return false;
        }
        g_pulse_opened = true;
    }
    else
    {
        pulse_dbg("dbg: period set\r\n");
        if (FSP_SUCCESS != R_GPT_PeriodSet(&g_pulse_ctrl, period_counts))
        {
            pulse_dbg("dbg: period set failed\r\n");
            return false;
        }
    }

    if (!g_pulse_running)
    {
        pulse_dbg("dbg: starting\r\n");
        R_GPT_Start(&g_pulse_ctrl);
        g_pulse_running = true;
    }

    pulse_dbg("dbg: done\r\n");
    return true;
}

void pulse_out_stop(void)
{
    if (g_pulse_running)
    {
        R_GPT_Stop(&g_pulse_ctrl);
        g_pulse_running = false;
    }
}
