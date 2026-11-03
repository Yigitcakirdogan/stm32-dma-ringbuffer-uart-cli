/**
 * @file    embedded_cli.c
 * @author  Yigit Cakirdogan
 * @brief   Deterministic zero-malloc command line shell implementation.
 */

#include "embedded_cli.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmd_help(embedded_cli_t *cli, int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    uart_dma_print(cli->uart, "Available commands:\r\n");
    for (size_t i = 0U; i < cli->command_count; i++) {
        char buf[128];
        (void)snprintf(buf, sizeof(buf), "  %-10s - %s\r\n",
                       cli->commands[i].name, cli->commands[i].help);
        uart_dma_print(cli->uart, buf);
    }
    return 0;
}

static int cmd_status(embedded_cli_t *cli, int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    char buf[160];
    (void)snprintf(buf, sizeof(buf),
                   "[SYS] Core: ARM Cortex-M4F @ 168 MHz\r\n"
                   "[SYS] IDLE IRQs: %lu | DMA HT: %lu | DMA TC: %lu\r\n"
                   "[SYS] Commands Executed: %lu\r\n",
                   (unsigned long)cli->uart->idle_irq_count,
                   (unsigned long)cli->uart->dma_ht_irq_count,
                   (unsigned long)cli->uart->dma_tc_irq_count,
                   (unsigned long)cli->commands_executed);
    uart_dma_print(cli->uart, buf);
    return 0;
}

static int cmd_mem(embedded_cli_t *cli, int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    char buf[192];
    (void)snprintf(buf, sizeof(buf),
                   "[MEM] RX Ring Capacity   : %u bytes\r\n"
                   "[MEM] Pending RX Bytes   : %u bytes\r\n"
                   "[MEM] RX High-Watermark  : %u bytes\r\n"
                   "[MEM] Overflow Drops     : %lu\r\n",
                   (unsigned)ring_buffer_capacity(&cli->uart->rx_ring),
                   (unsigned)ring_buffer_available(&cli->uart->rx_ring),
                   (unsigned)cli->uart->rx_ring.high_watermark,
                   (unsigned long)cli->uart->rx_ring.overflow_count);
    uart_dma_print(cli->uart, buf);
    return 0;
}

static int cmd_gpio(embedded_cli_t *cli, int argc, char *argv[])
{
    if (argc < 3) {
        uart_dma_print(cli->uart, "Usage: gpio <port A-E> <pin 0-15> [0|1]\r\n");
        return -1;
    }

    char port   = (char)toupper((unsigned char)argv[1][0]);
    int  pin    = atoi(argv[2]);

    if ((port < 'A') || (port > 'E') || (pin < 0) || (pin > 15)) {
        uart_dma_print(cli->uart, "[ERR] Invalid GPIO port or pin index\r\n");
        return -1;
    }

    char out[96];
    if (argc >= 4) {
        bool state = (atoi(argv[3]) != 0);
        if (cli->uart->hal_ops.gpio_write_pin != NULL) {
            cli->uart->hal_ops.gpio_write_pin(port, (uint8_t)pin, state);
        }
        (void)snprintf(out, sizeof(out),
                       "[GPIO] Port %c Pin %d -> %s\r\n",
                       port, pin, state ? "SET (1)" : "RESET (0)");
    } else {
        bool val = false;
        if (cli->uart->hal_ops.gpio_read_pin != NULL) {
            val = cli->uart->hal_ops.gpio_read_pin(port, (uint8_t)pin);
        }
        (void)snprintf(out, sizeof(out),
                       "[GPIO] Port %c Pin %d == %d\r\n",
                       port, pin, val ? 1 : 0);
    }

    uart_dma_print(cli->uart, out);
    return 0;
}

static int cmd_reboot(embedded_cli_t *cli, int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    uart_dma_print(cli->uart, "[SYS] Triggering NVIC_SystemReset()...\r\n");
    if (cli->uart->hal_ops.system_reset != NULL) {
        cli->uart->hal_ops.system_reset();
    }
    return 0;
}

static size_t tokenize_line(char *line, char *argv[], size_t max_args)
{
    size_t argc = 0U;
    char  *p    = line;

    while ((*p != '\0') && (argc < max_args)) {
        while ((*p != '\0') && isspace((unsigned char)*p)) {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while ((*p != '\0') && !isspace((unsigned char)*p)) {
            p++;
        }
    }
    return argc;
}

static void execute_line(embedded_cli_t *cli)
{
    char  *argv[CLI_MAX_ARGS];
    size_t argc = tokenize_line(cli->line_buf, argv, CLI_MAX_ARGS);

    if (argc == 0U) {
        return;
    }

    for (size_t i = 0U; i < cli->command_count; i++) {
        if (strcmp(argv[0], cli->commands[i].name) == 0) {
            cli->commands_executed++;
            (void)cli->commands[i].handler(cli, (int)argc, argv);
            return;
        }
    }

    uart_dma_print(cli->uart, "[CLI] Unknown command: '");
    uart_dma_print(cli->uart, argv[0]);
    uart_dma_print(cli->uart, "'. Type 'help' for list.\r\n");
}

bool embedded_cli_init(embedded_cli_t *cli, uart_dma_handle_t *uart, const char *prompt)
{
    if ((cli == NULL) || (uart == NULL)) {
        return false;
    }

    memset(cli, 0, sizeof(*cli));
    cli->uart   = uart;
    cli->prompt = (prompt != NULL) ? prompt : "mcu> ";

    static const cli_command_t builtin_cmds[] = {
        { "help",   "List registered diagnostic commands",       cmd_help   },
        { "status", "Display system clock & DMA IRQ telemetry",  cmd_status },
        { "mem",    "Inspect ring buffer occupancy & watermark", cmd_mem    },
        { "gpio",   "Read/Write GPIO pin: gpio <port> <pin> [v]", cmd_gpio  },
        { "reboot", "Trigger hardware system reset",             cmd_reboot }
    };

    for (size_t i = 0U; i < (sizeof(builtin_cmds) / sizeof(builtin_cmds[0])); i++) {
        (void)embedded_cli_register(cli, &builtin_cmds[i]);
    }

    return true;
}

bool embedded_cli_register(embedded_cli_t *cli, const cli_command_t *cmd)
{
    if ((cli == NULL) || (cmd == NULL) || (cmd->name == NULL) || (cmd->handler == NULL)) {
        return false;
    }
    if (cli->command_count >= CLI_MAX_COMMANDS) {
        return false;
    }

    cli->commands[cli->command_count++] = *cmd;
    return true;
}

void embedded_cli_process_char(embedded_cli_t *cli, char ch)
{
    if (cli == NULL) {
        return;
    }

    if ((ch == '\r') || (ch == '\n')) {
        uart_dma_print(cli->uart, "\r\n");
        cli->line_buf[cli->line_pos] = '\0';
        if (cli->line_pos > 0U) {
            execute_line(cli);
        }
        cli->line_pos = 0U;
        uart_dma_print(cli->uart, cli->prompt);
    } else if ((ch == '\b') || (ch == 0x7F)) {
        if (cli->line_pos > 0U) {
            cli->line_pos--;
            uart_dma_print(cli->uart, "\b \b");
        }
    } else if ((ch >= 0x20) && (ch <= 0x7E)) {
        if (cli->line_pos < (CLI_MAX_LINE_LENGTH - 1U)) {
            cli->line_buf[cli->line_pos++] = ch;
            uint8_t echo = (uint8_t)ch;
            (void)uart_dma_write(cli->uart, &echo, 1U);
        }
    }
}

void embedded_cli_poll(embedded_cli_t *cli)
{
    if (cli == NULL) {
        return;
    }

    uint8_t ch;
    while (ring_buffer_get(&cli->uart->rx_ring, &ch)) {
        embedded_cli_process_char(cli, (char)ch);
    }
}
