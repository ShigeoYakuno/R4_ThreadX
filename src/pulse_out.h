#ifndef PULSE_OUT_H_
#define PULSE_OUT_H_

#include <stdbool.h>
#include <stdint.h>

/* Bounds on the period pulse_out_set_period_ns() accepts. */
#define PULSE_OUT_PERIOD_NS_MIN 1000UL     /* 1us */
#define PULSE_OUT_PERIOD_NS_MAX 10000000UL /* 10ms */

/* Switches D12 (GTIOC6B) to GPT's alternate function. Does NOT open GPT6 itself -- that happens
 * lazily, inside the first pulse_out_set_period_ns() call, using the real requested period (see
 * pulse_out.c for why). Requires R_IOPORT_Open() to have already run. Must be called from a
 * ThreadX thread (see uart_init()'s comment in uart.h for why). */
void pulse_out_init(void);

/* (Re)configures and (re)starts a fixed 50%-duty square wave on D12 with the given period. The
 * first call opens GPT6 (R_GPT_Open()) with this period baked in; every call after that only
 * calls R_GPT_PeriodSet() -- GPT6 is never re-opened/closed again, so this is safe to call
 * repeatedly and glitch-free. GPT6's clock divider is fixed at /16 (3MHz), which gives an exact,
 * non-rounded count for every whole-microsecond period in [PULSE_OUT_PERIOD_NS_MIN,
 * PULSE_OUT_PERIOD_NS_MAX]. Returns false and leaves the current output unchanged if period_ns is
 * outside that range. Not thread-safe (no internal locking); call only from one thread
 * (uart_cmd_thread). */
bool pulse_out_set_period_ns(uint32_t period_ns);

/* Stops the output; D12 is held low. */
void pulse_out_stop(void);

#endif /* PULSE_OUT_H_ */
