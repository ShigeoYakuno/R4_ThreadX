#include "hal_data.h"
#include "tx_api.h"

#include "led_mtx.h"
#include "led_thread.h"
#include "log_thread.h"
#include "pulse_out.h"
#include "trigger_out.h"
#include "uart.h"
#include "uart_cmd_thread.h"
#include "vector_table.h"

#define FW_VERSION "V1.01"

/* led_thread never blocks (it spins scanning the LED matrix forever), so it must be the lowest
 * priority: uart_cmd_thread and log_thread mostly sleep/wait and only need the CPU briefly, so
 * they must be able to preempt it whenever they wake up. See docs/threadx_construction.md for the
 * bug this exact mistake caused previously. */
#define LED_THREAD_PRIORITY      10
#define UART_CMD_THREAD_PRIORITY 5
#define LOG_THREAD_PRIORITY      4

static TX_THREAD led_thread;
static TX_THREAD log_thread;
static TX_THREAD uart_cmd_thread;

static uint8_t led_thread_stack[512];
static uint8_t log_thread_stack[1024];
/* Bumped from 1024: the pulse-hang bug (see docs/architecture.md) looked consistent with a stack
 * overflow on the R_GPT_Close()+R_GPT_Open() reconfigure path (handle_pulse_command ->
 * pulse_out_set_period_ns -> nested r_gpt.c calls), which is only reached from this thread. RAM
 * has ample headroom (32KB total) to be generous here. */
static uint8_t uart_cmd_thread_stack[4096];

/* Registered with tx_thread_stack_error_notify() below: if ThreadX's runtime stack checking
 * (TX_ENABLE_STACK_CHECKING, tx_user.h) ever detects an overflow on any thread, report it instead
 * of silently corrupting memory. */
static void stack_error_handler(TX_THREAD * thread_ptr)
{
    log_printf("FATAL: stack overflow in thread '%s'\r\n", thread_ptr->tx_thread_name);
}

void tx_application_define(void * first_unused_memory)
{
    (void) first_unused_memory;

    uart_init();
    pulse_out_init();
    trigger_out_init();
    tx_thread_stack_error_notify(stack_error_handler);

    /* Queued into the ring buffer here; log_thread (created below) flushes it to UART once it
     * starts running. */
    log_printf("Firmware version: %s\r\n", FW_VERSION);

    led_thread_init();

    tx_thread_create(&led_thread, "led", led_thread_entry, 0,
                      led_thread_stack, sizeof(led_thread_stack),
                      LED_THREAD_PRIORITY, LED_THREAD_PRIORITY,
                      TX_NO_TIME_SLICE, TX_AUTO_START);

    tx_thread_create(&log_thread, "log", log_thread_entry, 0,
                      log_thread_stack, sizeof(log_thread_stack),
                      LOG_THREAD_PRIORITY, LOG_THREAD_PRIORITY,
                      TX_NO_TIME_SLICE, TX_AUTO_START);

    tx_thread_create(&uart_cmd_thread, "uart cmd", uart_cmd_thread_entry, 0,
                      uart_cmd_thread_stack, sizeof(uart_cmd_thread_stack),
                      UART_CMD_THREAD_PRIORITY, UART_CMD_THREAD_PRIORITY,
                      TX_NO_TIME_SLICE, TX_AUTO_START);
}

void hal_entry(void)
{
    /* Must run before anything that relies on an interrupt (uart_init()'s SCI2 IRQs): sets up
     * this project's own interrupt vector table. See vector_table.c for why it's needed. */
    copy_vectors_to_ram();

    /* Apply the board's pin configuration (g_bsp_pin_cfg) before using any GPIO. */
    R_IOPORT_Open(&g_ioport_ctrl, g_ioport.p_cfg);

    led_mtx_init();

    /* Never returns; tx_application_define() creates the threads above. */
    tx_kernel_enter();
}
