#ifndef UART_H_
#define UART_H_

#include <stddef.h>
#include <stdint.h>

/* Opens the SCI2 UART on D0(RX)/D1(TX) at 115200 8N1. Requires copy_vectors_to_ram() and
 * R_IOPORT_Open() to have already run. Must be called from a ThreadX thread (creates kernel
 * objects), so call it from hal_entry() after tx threads exist, or from tx_application_define(). */
void uart_init(void);

/* Blocks the calling thread until all bytes have been transmitted. Safe to call concurrently
 * from multiple threads (serializes internally), but never from an ISR. */
void uart_write_blocking(const uint8_t * data, size_t len);

/* Blocks the calling thread until one byte has been received. */
uint8_t uart_rx_getc_blocking(void);

#endif /* UART_H_ */
