#include "uart_cmd_thread.h"
#include "led_thread.h"
#include "log_thread.h"
#include "uart.h"

#include <stddef.h>
#include <string.h>
#include <strings.h>

#define CMD_LINE_MAX 64

static void handle_command(const char * line)
{
    if (line[0] == '\0')
    {
        return;
    }

    if (0 == strcasecmp(line, "led1"))
    {
        led_thread_set_shape(LED_SHAPE_HEART);
        log_printf("OK: shape=heart\r\n");
    }
    else if (0 == strcasecmp(line, "led2"))
    {
        led_thread_set_shape(LED_SHAPE_DIAMOND);
        log_printf("OK: shape=diamond\r\n");
    }
    else if (0 == strcasecmp(line, "led3"))
    {
        led_thread_set_shape(LED_SHAPE_TRIANGLE);
        log_printf("OK: shape=triangle\r\n");
    }
    else if (0 == strcasecmp(line, "led4"))
    {
        led_thread_set_shape(LED_SHAPE_SQUARE);
        log_printf("OK: shape=square\r\n");
    }
    else if ((0 == strcasecmp(line, "help")) || (0 == strcmp(line, "?")))
    {
        log_printf("commands: led1=heart led2=diamond led3=triangle led4=square help/?\r\n");
    }
    else
    {
        log_printf("ERR: unknown command '%s'\r\n", line);
    }
}

void uart_cmd_thread_entry(ULONG thread_input)
{
    (void) thread_input;

    char   line[CMD_LINE_MAX];
    size_t len = 0;

    while (1)
    {
        uint8_t c = uart_rx_getc_blocking();

        if ((c == '\r') || (c == '\n'))
        {
            uart_write_blocking((const uint8_t *) "\r\n", 2);
            line[len] = '\0';
            handle_command(line);
            len = 0;
        }
        else if ((c == 0x08) || (c == 0x7F)) /* backspace / delete */
        {
            if (len > 0)
            {
                len--;
                uart_write_blocking((const uint8_t *) "\b \b", 3);
            }
        }
        else if ((c >= 0x20) && (c < 0x7F)) /* printable ASCII */
        {
            if (len < (CMD_LINE_MAX - 1))
            {
                line[len++] = (char) c;
                uart_write_blocking(&c, 1);
            }
        }
        /* other control characters are silently ignored */
    }
}
