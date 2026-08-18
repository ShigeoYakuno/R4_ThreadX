#include "trigger_out.h"
#include "hal_data.h"

#define TRIGGER_OUT_PIN BSP_IO_PORT_01_PIN_02 /* D13 */

void trigger_out_init(void)
{
    R_BSP_PinCfg(TRIGGER_OUT_PIN, IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);
}

void trigger_out_set(bool on)
{
    R_BSP_PinWrite(TRIGGER_OUT_PIN, on ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}

void trigger_out_shot(void)
{
    R_BSP_PinWrite(TRIGGER_OUT_PIN, BSP_IO_LEVEL_LOW);
    R_BSP_SoftwareDelay(10U, BSP_DELAY_UNITS_MICROSECONDS);
    R_BSP_PinWrite(TRIGGER_OUT_PIN, BSP_IO_LEVEL_HIGH);
}
