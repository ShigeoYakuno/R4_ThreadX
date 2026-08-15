#ifndef LED_THREAD_H_
#define LED_THREAD_H_

#include "tx_api.h"

typedef enum e_led_shape
{
    LED_SHAPE_HEART = 1,
    LED_SHAPE_DIAMOND,
    LED_SHAPE_TRIANGLE,
    LED_SHAPE_SQUARE,
} led_shape_t;

/* Creates the shape-request queue. Call once from tx_application_define(), before any thread
 * that might call led_thread_set_shape() starts running. */
void led_thread_init(void);

/* Requests led_thread switch the matrix to the given shape. Safe from any thread context. */
void led_thread_set_shape(led_shape_t shape);

/* TX_THREAD entry function. Never blocks (continuously scans the matrix), so must be created at
 * the lowest priority among this project's threads. */
void led_thread_entry(ULONG thread_input);

#endif /* LED_THREAD_H_ */
