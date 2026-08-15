#include "led_thread.h"
#include "led_mtx.h"

#include <stdint.h>

static const uint8_t shape_heart[LED_MTX_HEIGHT][LED_MTX_WIDTH] = {
    { 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0 },
    { 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
    { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 },
    { 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0 },
};

static const uint8_t shape_diamond[LED_MTX_HEIGHT][LED_MTX_WIDTH] = {
    { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0 },
    { 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0 },
    { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 },
    { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 },
    { 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
};

static const uint8_t shape_triangle[LED_MTX_HEIGHT][LED_MTX_WIDTH] = {
    { 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0 },
    { 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0 },
    { 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0 },
    { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 },
    { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 },
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
};

static const uint8_t shape_square[LED_MTX_HEIGHT][LED_MTX_WIDTH] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
    { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 },
    { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 },
    { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 },
    { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 },
    { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

typedef const uint8_t (* shape_bitmap_t)[LED_MTX_WIDTH];

static void draw_shape(shape_bitmap_t bitmap)
{
    for (uint8_t y = 0; y < LED_MTX_HEIGHT; y++)
    {
        for (uint8_t x = 0; x < LED_MTX_WIDTH; x++)
        {
            led_mtx_set(x, y, bitmap[y][x] != 0);
        }
    }
}

#define LED_SHAPE_QUEUE_DEPTH 4

static TX_QUEUE g_shape_queue;
static ULONG    g_shape_queue_storage[LED_SHAPE_QUEUE_DEPTH];

void led_thread_init(void)
{
    tx_queue_create(&g_shape_queue, "led shape", TX_1_ULONG,
                     g_shape_queue_storage, sizeof(g_shape_queue_storage));
}

void led_thread_set_shape(led_shape_t shape)
{
    ULONG msg = (ULONG) shape;

    tx_queue_send(&g_shape_queue, &msg, TX_NO_WAIT);
}

void led_thread_entry(ULONG thread_input)
{
    (void) thread_input;

    draw_shape(shape_heart);

    while (1)
    {
        ULONG msg;

        while (TX_SUCCESS == tx_queue_receive(&g_shape_queue, &msg, TX_NO_WAIT))
        {
            switch ((led_shape_t) msg)
            {
                case LED_SHAPE_HEART:
                {
                    draw_shape(shape_heart);
                    break;
                }

                case LED_SHAPE_DIAMOND:
                {
                    draw_shape(shape_diamond);
                    break;
                }

                case LED_SHAPE_TRIANGLE:
                {
                    draw_shape(shape_triangle);
                    break;
                }

                case LED_SHAPE_SQUARE:
                {
                    draw_shape(shape_square);
                    break;
                }

                default:
                {
                    break;
                }
            }
        }

        led_mtx_refresh();
    }
}
