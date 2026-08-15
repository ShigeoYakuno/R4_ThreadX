#include "uart.h"
#include "hal_data.h"
#include "tx_api.h"

#define UART_CHANNEL         2
#define UART_BAUDRATE        115200
#define UART_BAUD_ERROR_X1000 5000 /* 5.0% max error, matches the e2studio configurator default */
#define UART_RX_QUEUE_DEPTH  64
#define UART_IPL             12

static sci_uart_instance_ctrl_t g_uart_ctrl;
static baud_setting_t           g_uart_baud_setting;

static sci_uart_extended_cfg_t g_uart_cfg_extend = {
    .clock            = SCI_UART_CLOCK_INT,
    .rx_edge_start    = SCI_UART_START_BIT_FALLING_EDGE,
    .noise_cancel     = SCI_UART_NOISE_CANCELLATION_DISABLE,
    .p_baud_setting   = &g_uart_baud_setting,
    .rx_fifo_trigger  = SCI_UART_RX_FIFO_TRIGGER_1,
    .flow_control_pin = (bsp_io_port_pin_t) 0xFFFFU, /* r_sci_uart's private SCI_UART_INVALID_16BIT_PARAM sentinel */
    .flow_control     = SCI_UART_FLOW_CONTROL_RTS, /* not asserted unless flow_control_pin is valid */
    .rs485_setting    = { .enable = SCI_UART_RS485_DISABLE },
};

static void uart_event_callback(uart_callback_args_t * p_args);

static const uart_cfg_t g_uart_cfg = {
    .channel    = UART_CHANNEL,
    .data_bits  = UART_DATA_BITS_8,
    .parity     = UART_PARITY_OFF,
    .stop_bits  = UART_STOP_BITS_1,
    .rxi_ipl    = UART_IPL,
    .rxi_irq    = (IRQn_Type) VECTOR_NUMBER_SCI2_RXI,
    .txi_ipl    = UART_IPL,
    .txi_irq    = (IRQn_Type) VECTOR_NUMBER_SCI2_TXI,
    .tei_ipl    = UART_IPL,
    .tei_irq    = (IRQn_Type) VECTOR_NUMBER_SCI2_TEI,
    .eri_ipl    = UART_IPL,
    .eri_irq    = (IRQn_Type) VECTOR_NUMBER_SCI2_ERI,
    .p_transfer_rx = NULL,
    .p_transfer_tx = NULL,
    .p_callback = uart_event_callback,
    .p_context  = NULL,
    .p_extend   = &g_uart_cfg_extend,
};

static TX_QUEUE g_uart_rx_queue;
static ULONG    g_uart_rx_queue_storage[UART_RX_QUEUE_DEPTH];

static TX_SEMAPHORE g_uart_tx_done_sem;
static TX_MUTEX      g_uart_tx_mutex;

static void uart_event_callback(uart_callback_args_t * p_args)
{
    switch (p_args->event)
    {
        case UART_EVENT_TX_COMPLETE:
        {
            tx_semaphore_put(&g_uart_tx_done_sem);
            break;
        }

        case UART_EVENT_RX_CHAR:
        {
            /* Queue is sized generously; if the reader falls behind we simply drop the byte
             * rather than block the ISR. */
            tx_queue_send(&g_uart_rx_queue, &p_args->data, TX_NO_WAIT);
            break;
        }

        default:
        {
            break;
        }
    }
}

void uart_init(void)
{
    tx_queue_create(&g_uart_rx_queue, "uart rx", TX_1_ULONG,
                     g_uart_rx_queue_storage, sizeof(g_uart_rx_queue_storage));
    tx_semaphore_create(&g_uart_tx_done_sem, "uart tx done", 0);
    tx_mutex_create(&g_uart_tx_mutex, "uart tx", TX_NO_INHERIT);

    /* D0 (RXD) is already configured for the SCI0_2_4_6_8 peripheral group by g_bsp_pin_cfg,
     * applied via R_IOPORT_Open() in hal_entry(). D1 (TXD) is not in that table, so it is set up
     * here the same way led_mtx.c configures its GPIOs directly with R_BSP_PinCfg(). */
    R_BSP_PinCfg(BSP_IO_PORT_03_PIN_02, IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_SCI0_2_4_6_8);

    R_SCI_UART_BaudCalculate(UART_BAUDRATE, false, UART_BAUD_ERROR_X1000, &g_uart_baud_setting);

    R_SCI_UART_Open(&g_uart_ctrl, &g_uart_cfg);
}

void uart_write_blocking(const uint8_t * data, size_t len)
{
    tx_mutex_get(&g_uart_tx_mutex, TX_WAIT_FOREVER);

    R_SCI_UART_Write(&g_uart_ctrl, data, len);
    tx_semaphore_get(&g_uart_tx_done_sem, TX_WAIT_FOREVER);

    tx_mutex_put(&g_uart_tx_mutex);
}

uint8_t uart_rx_getc_blocking(void)
{
    ULONG byte = 0;

    tx_queue_receive(&g_uart_rx_queue, &byte, TX_WAIT_FOREVER);

    return (uint8_t) byte;
}
