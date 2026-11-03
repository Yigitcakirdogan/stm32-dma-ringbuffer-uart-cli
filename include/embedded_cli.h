/**
 * @file    embedded_cli.h
 * @author  Yigit Cakirdogan
 * @brief   Deterministic, zero-heap-allocation command-line shell for embedded systems.
 */

#ifndef EMBEDDED_CLI_H
#define EMBEDDED_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "uart_dma_driver.h"

#define CLI_MAX_LINE_LENGTH  128U
#define CLI_MAX_ARGS         8U
#define CLI_MAX_COMMANDS     16U

struct embedded_cli_s;

typedef int (*cli_cmd_handler_t)(struct embedded_cli_s *cli, int argc, char *argv[]);

typedef struct {
    const char       *name;
    const char       *help;
    cli_cmd_handler_t handler;
} cli_command_t;

typedef struct embedded_cli_s {
    uart_dma_handle_t   *uart;
    char                 line_buf[CLI_MAX_LINE_LENGTH];
    size_t               line_pos;
    cli_command_t        commands[CLI_MAX_COMMANDS];
    size_t               command_count;
    uint32_t             commands_executed;
    const char          *prompt;
} embedded_cli_t;

/**
 * @brief Initialize the CLI engine and register built-in diagnostic commands.
 */
bool embedded_cli_init(embedded_cli_t *cli, uart_dma_handle_t *uart, const char *prompt);

/**
 * @brief Register a custom command into the static command table.
 */
bool embedded_cli_register(embedded_cli_t *cli, const cli_command_t *cmd);

/**
 * @brief Process all currently pending bytes in the UART RX ring buffer.
 */
void embedded_cli_poll(embedded_cli_t *cli);

/**
 * @brief Feed a single character into the CLI lexer/line editor.
 */
void embedded_cli_process_char(embedded_cli_t *cli, char ch);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDDED_CLI_H */
