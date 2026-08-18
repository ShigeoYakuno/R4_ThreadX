#ifndef TRIGGER_OUT_H_
#define TRIGGER_OUT_H_

#include <stdbool.h>

/* Configures D13 as a push-pull GPIO output, driven low (off). Requires R_IOPORT_Open() to
 * have already run (same constraint as uart_init(), see uart.h). */
void trigger_out_init(void);

/* Drives D13 high (on) or low (off). Plain GPIO write, safe to call from any thread. */
void trigger_out_set(bool on);

void trigger_out_shot(void);

#endif /* TRIGGER_OUT_H_ */
