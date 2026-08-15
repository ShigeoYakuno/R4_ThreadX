#include "led_mtx.h"
#include "hal_data.h"

/*
 * The UNO R4 WiFi's 12x8 LED matrix has no separate driver chip: 96 LEDs are
 * charlieplexed directly across 11 RA4M1 GPIOs (P003, P004, P011, P012, P013,
 * P015, P204, P205, P206, P212, P213). Only one LED can be truly driven at a
 * time (one pin sourcing current, one sinking it, the other 9 left Hi-Z), so
 * a full picture is built by fast round-robin scanning + persistence of
 * vision, same as Arduino's own Arduino_LED_Matrix library does.
 *
 * The pin order below and the 96-entry anode/cathode table are taken from
 * Arduino's official ArduinoCore-renesas driver (libraries/Arduino_LED_Matrix),
 * since the physical charlieplex wiring can't be derived from the FSP pin
 * config alone.
 */
static const bsp_io_port_pin_t matrix_pins[11] = {
    BSP_IO_PORT_00_PIN_03, BSP_IO_PORT_00_PIN_04, BSP_IO_PORT_00_PIN_11,
    BSP_IO_PORT_00_PIN_12, BSP_IO_PORT_00_PIN_13, BSP_IO_PORT_00_PIN_15,
    BSP_IO_PORT_02_PIN_04, BSP_IO_PORT_02_PIN_05, BSP_IO_PORT_02_PIN_06,
    BSP_IO_PORT_02_PIN_12, BSP_IO_PORT_02_PIN_13,
};

/* Each entry is { anode pin index, cathode pin index } into matrix_pins[],
 * for LED index (y * LED_MTX_WIDTH + x). */
static const uint8_t matrix_led_pins[LED_MTX_WIDTH * LED_MTX_HEIGHT][2] = {
    { 7, 3 }, { 3, 7 }, { 7, 4 }, { 4, 7 }, { 3, 4 }, { 4, 3 }, { 7, 8 }, { 8, 7 },
    { 3, 8 }, { 8, 3 }, { 4, 8 }, { 8, 4 }, { 7, 0 }, { 0, 7 }, { 3, 0 }, { 0, 3 },
    { 4, 0 }, { 0, 4 }, { 8, 0 }, { 0, 8 }, { 7, 6 }, { 6, 7 }, { 3, 6 }, { 6, 3 },
    { 4, 6 }, { 6, 4 }, { 8, 6 }, { 6, 8 }, { 0, 6 }, { 6, 0 }, { 7, 5 }, { 5, 7 },
    { 3, 5 }, { 5, 3 }, { 4, 5 }, { 5, 4 }, { 8, 5 }, { 5, 8 }, { 0, 5 }, { 5, 0 },
    { 6, 5 }, { 5, 6 }, { 7, 1 }, { 1, 7 }, { 3, 1 }, { 1, 3 }, { 4, 1 }, { 1, 4 },
    { 8, 1 }, { 1, 8 }, { 0, 1 }, { 1, 0 }, { 6, 1 }, { 1, 6 }, { 5, 1 }, { 1, 5 },
    { 7, 2 }, { 2, 7 }, { 3, 2 }, { 2, 3 }, { 4, 2 }, { 2, 4 }, { 8, 2 }, { 2, 8 },
    { 0, 2 }, { 2, 0 }, { 6, 2 }, { 2, 6 }, { 5, 2 }, { 2, 5 }, { 1, 2 }, { 2, 1 },
    { 7, 10 }, { 10, 7 }, { 3, 10 }, { 10, 3 }, { 4, 10 }, { 10, 4 }, { 8, 10 }, { 10, 8 },
    { 0, 10 }, { 10, 0 }, { 6, 10 }, { 10, 6 }, { 5, 10 }, { 10, 5 }, { 1, 10 }, { 10, 1 },
    { 2, 10 }, { 10, 2 }, { 7, 9 }, { 9, 7 }, { 3, 9 }, { 9, 3 }, { 4, 9 }, { 9, 4 },
};

static bool framebuffer[LED_MTX_HEIGHT][LED_MTX_WIDTH];

static void pin_hi_z(bsp_io_port_pin_t pin)
{
    R_BSP_PinCfg(pin, IOPORT_CFG_PORT_DIRECTION_INPUT);
}

static void all_pins_hi_z(void)
{
    for (uint8_t i = 0; i < 11; i++)
    {
        pin_hi_z(matrix_pins[i]);
    }
}

static void led_on(uint8_t led_index)
{
    bsp_io_port_pin_t anode   = matrix_pins[matrix_led_pins[led_index][0]];
    bsp_io_port_pin_t cathode = matrix_pins[matrix_led_pins[led_index][1]];

    R_BSP_PinCfg(anode, IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_HIGH);
    R_BSP_PinCfg(cathode, IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);
}

void led_mtx_init(void)
{
    R_BSP_PinAccessEnable();
    all_pins_hi_z();
    led_mtx_clear();
}

void led_mtx_clear(void)
{
    for (uint8_t y = 0; y < LED_MTX_HEIGHT; y++)
    {
        for (uint8_t x = 0; x < LED_MTX_WIDTH; x++)
        {
            framebuffer[y][x] = false;
        }
    }
}

void led_mtx_set(uint8_t x, uint8_t y, bool on)
{
    if ((x >= LED_MTX_WIDTH) || (y >= LED_MTX_HEIGHT))
    {
        return;
    }
    framebuffer[y][x] = on;
}

void led_mtx_refresh(void)
{
    for (uint8_t y = 0; y < LED_MTX_HEIGHT; y++)
    {
        for (uint8_t x = 0; x < LED_MTX_WIDTH; x++)
        {
            if (framebuffer[y][x])
            {
                led_on((uint8_t) (y * LED_MTX_WIDTH + x));
                R_BSP_SoftwareDelay(300, BSP_DELAY_UNITS_MICROSECONDS);
                all_pins_hi_z();
            }
        }
    }
}
