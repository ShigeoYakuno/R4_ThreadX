#include "uart_cmd_thread.h"
#include "led_thread.h"
#include "log_thread.h"
#include "pulse_out.h"
#include "trigger_out.h"
#include "uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CMD_LINE_MAX 64

/* Parses a numeric string with a trailing "us" or "ms" suffix (e.g. "500us") into a period in
 * nanoseconds. Returns false if the string isn't of that form. */
static bool parse_pulse_period_ns(const char * arg, uint32_t * out_ns)
{
    char *        end   = NULL;
    unsigned long value = strtoul(arg, &end, 10);

    if (end == arg)
    {
        return false;
    }

    if (0 == strcasecmp(end, "ns"))
    {
        *out_ns = (uint32_t) value;
        return true;
    }

    if (0 == strcasecmp(end, "us"))
    {
        if (value > (UINT32_MAX / 1000UL))
        {
            return false;
        }

        *out_ns = (uint32_t) (value * 1000UL);
        return true;
    }

    if (0 == strcasecmp(end, "ms"))
    {
        if (value > (UINT32_MAX / 1000000UL))
        {
            return false;
        }

        *out_ns = (uint32_t) (value * 1000000UL);
        return true;
    }

    return false;
}

static void handle_pulse_command(const char * arg)
{
    if (0 == strcasecmp(arg, "off"))
    {
        pulse_out_stop();
        log_printf("OK: pulse=off\r\n");
        return;
    }

    uint32_t period_ns;
    if (parse_pulse_period_ns(arg, &period_ns) && pulse_out_set_period_ns(period_ns))
    {
        log_printf("OK: pulse period=%s (D12)\r\n", arg);
    }
    else
    {
        log_printf("ERR: usage: pulse <n>us|<n>ms (2us-20us) | pulse off\r\n");
    }
}

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
    else if (0 == strcasecmp(line, "pulse"))
    {
        log_printf("ERR: usage: pulse <n>us|<n>ms (1us-10ms) | pulse off\r\n");
    }
    else if (0 == strncasecmp(line, "pulse ", 6))
    {
        handle_pulse_command(line + 6);
    }
    else if (0 == strcasecmp(line, "trg shot"))
    {
        trigger_out_shot();
        log_printf("OK: trg=shot (Low 10us -> High)\r\n");
    }
    else if (0 == strcasecmp(line, "trg on"))
    {
        trigger_out_set(true);
        log_printf("OK: trg=on\r\n");
    }
    else if (0 == strcasecmp(line, "trg off"))
    {
        trigger_out_set(false);
        log_printf("OK: trg=off\r\n");
    }
    else if (0 == strcasecmp(line, "trg"))
    {
        log_printf("ERR: usage: trg shot|on|off\r\n");
    }
    else if ((0 == strcasecmp(line, "help")) || (0 == strcmp(line, "?")))
    {
        log_printf("commands: led1=heart led2=diamond led3=triangle led4=square "
                "pulse <n>ns|<n>us|off (D12) "
                "trg shot|on|off (D13) help/?\r\n");
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
            /* Echoback disabled: this UART is now shared with another MCU, not just an
             * interactive terminal, so unsolicited bytes back to the sender aren't wanted.
             * Command responses (OK:/ERR: via log_printf) still go out as before. */
            /* uart_write_blocking((const uint8_t *) "\r\n", 2); */
            line[len] = '\0';
            handle_command(line);
            len = 0;
        }
        else if ((c == 0x08) || (c == 0x7F)) /* backspace / delete */
        {
            if (len > 0)
            {
                len--;
                /* uart_write_blocking((const uint8_t *) "\b \b", 3); */
            }
        }
        else if ((c >= 0x20) && (c < 0x7F)) /* printable ASCII */
        {
            if (len < (CMD_LINE_MAX - 1))
            {
                line[len++] = (char) c;
                /* uart_write_blocking(&c, 1); */
            }
        }
        /* other control characters are silently ignored */
    }
}
