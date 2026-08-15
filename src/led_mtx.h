#ifndef LED_MTX_H_
#define LED_MTX_H_

#include <stdbool.h>
#include <stdint.h>

#define LED_MTX_WIDTH  12
#define LED_MTX_HEIGHT 8

/* Configures the 11 charlieplex pins as GPIO inputs (Hi-Z) and clears the framebuffer. */
void led_mtx_init(void);

void led_mtx_clear(void);
void led_mtx_set(uint8_t x, uint8_t y, bool on);

/* Scans every lit pixel once (one charlieplex pulse each). Call this repeatedly
 * from the main loop; persistence of vision is what makes the shape look stable. */
void led_mtx_refresh(void);

#endif /* LED_MTX_H_ */
