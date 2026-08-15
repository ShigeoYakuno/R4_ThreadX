#ifndef UART_CMD_THREAD_H_
#define UART_CMD_THREAD_H_

#include "tx_api.h"

/* TX_THREAD entry function. Blocks on UART RX most of the time, so give it a higher priority
 * than led_thread (which never blocks). */
void uart_cmd_thread_entry(ULONG thread_input);

#endif /* UART_CMD_THREAD_H_ */
