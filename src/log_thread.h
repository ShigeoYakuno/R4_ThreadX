#ifndef LOG_THREAD_H_
#define LOG_THREAD_H_

#include "tx_api.h"

/* Formats and appends a line to the log ring buffer. Thread context only (uses vsnprintf) --
 * do not call from an ISR, use log_write_isr() there instead. Returns immediately; the actual
 * UART transmission happens later on log_thread. */
void log_printf(const char * fmt, ...);

/* ISR-safe: appends a pre-formatted, NUL-terminated string to the log ring buffer without any
 * formatting work, so it is safe to call from interrupt context. */
void log_write_isr(const char * msg);

/* TX_THREAD entry function; periodically flushes the ring buffer to the UART. */
void log_thread_entry(ULONG thread_input);

#endif /* LOG_THREAD_H_ */
