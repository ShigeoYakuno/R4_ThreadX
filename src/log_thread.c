#include "log_thread.h"
#include "uart.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Ring buffer for log lines, modeled after reference/log_task.c's queue-based syslog: producers
 * (any thread, or an ISR via log_write_isr()) only ever touch this buffer under a short
 * interrupt-disable critical section, so they never block on the UART. log_thread is the sole
 * consumer and does the actual (blocking) UART write, so slow/contended UART traffic never stalls
 * whoever is logging.
 */
#define LOG_RING_SIZE           1024
#define LOG_LINE_MAX            256
#define LOG_FLUSH_PERIOD_TICKS  5 /* 50ms at the default 100 ticks/sec */

static char   log_ring[LOG_RING_SIZE];
static size_t log_head; /* oldest unread byte */
static size_t log_len;  /* bytes currently buffered */

static uint8_t log_flush_buf[LOG_RING_SIZE];

static void log_ring_append(const char * data, size_t len)
{
    TX_INTERRUPT_SAVE_AREA;
    TX_DISABLE;

    for (size_t i = 0; i < len; i++)
    {
        if (log_len >= LOG_RING_SIZE)
        {
            break; /* buffer full: drop the rest rather than overwrite unread data */
        }

        size_t pos = (log_head + log_len) % LOG_RING_SIZE;
        log_ring[pos] = data[i];
        log_len++;
    }

    TX_RESTORE;
}

void log_printf(const char * fmt, ...)
{
    char    msg[LOG_LINE_MAX];
    va_list args;

    va_start(args, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (n <= 0)
    {
        return;
    }

    size_t len = (size_t) n;
    if (len >= sizeof(msg))
    {
        len = sizeof(msg) - 1;
    }

    log_ring_append(msg, len);
}

void log_write_isr(const char * msg)
{
    log_ring_append(msg, strlen(msg));
}

void log_thread_entry(ULONG thread_input)
{
    (void) thread_input;

    while (1)
    {
        tx_thread_sleep(LOG_FLUSH_PERIOD_TICKS);

        size_t to_send;
        TX_INTERRUPT_SAVE_AREA;
        TX_DISABLE;

        to_send = log_len;
        for (size_t i = 0; i < to_send; i++)
        {
            log_flush_buf[i] = (uint8_t) log_ring[(log_head + i) % LOG_RING_SIZE];
        }

        log_head = (log_head + to_send) % LOG_RING_SIZE;
        log_len -= to_send;

        TX_RESTORE;

        if (to_send > 0)
        {
            uart_write_blocking(log_flush_buf, to_send);
        }
    }
}
