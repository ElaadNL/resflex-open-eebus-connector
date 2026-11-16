// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/cli.h
 *
 * Simple readline-based CLI framework for EEBus demo programs.
 *
 * Usage:
 *   cli_t* cli = cli_create("HP");
 *   cli_register(cli, "power", cmd_power, "power <W>  Set simulated power");
 *   cli_register(cli, "quit",  cmd_quit,  "quit       Exit");
 *   cli_run(cli);   // blocks until cli_quit() is called
 *   cli_destroy(cli);
 */

#ifndef EEBUS_DEMO_CLI_H_
#define EEBUS_DEMO_CLI_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cli cli_t;

/**
 * @brief Command handler callback.
 *
 * @param argc      Number of tokens (including the command name).
 * @param argv      Token array; argv[0] is the command name.
 * @param userdata  Value passed to cli_register().
 */
typedef void (*cli_handler_fn)(int argc, char** argv, void* userdata);

/**
 * @brief Create a CLI instance.
 *
 * @param prompt  Prompt string shown to the user, e.g. "HP".
 *                The actual prompt will be "[HP] > ".
 * @return New cli_t, or NULL on allocation failure.
 */
cli_t* cli_create(const char* prompt);

/**
 * @brief Free all resources owned by the CLI.
 *
 * Safe to call after cli_run() returns.
 */
void cli_destroy(cli_t* cli);

/**
 * @brief Register a command.
 *
 * @param cli       CLI instance.
 * @param name      Command name (first token the user types).
 * @param handler   Callback invoked when the command is entered.
 * @param help      One-line help string shown by the built-in "help" command.
 * @param userdata  Forwarded to handler.
 * @return 0 on success, -1 if the command table is full.
 */
int cli_register(
    cli_t*          cli,
    const char*     name,
    cli_handler_fn  handler,
    const char*     help,
    void*           userdata);

/**
 * @brief Blank-line callback.
 *
 * @param userdata  Value passed to cli_set_blank_handler().
 */
typedef void (*cli_blank_fn)(void* userdata);

/**
 * @brief Register a handler invoked when the user enters an empty line.
 *
 * Optional; if not set, blank lines are silently skipped (the original
 * behavior). Pass handler=NULL to unregister.
 */
void cli_set_blank_handler(cli_t* cli, cli_blank_fn handler, void* userdata);

/**
 * @brief Enable persistent command history.
 *
 * Loads ~/.eebus/<demo_name>/history at cli_run() start and writes it
 * back when cli_run() returns. The directory is created if missing
 * (mode 0700). No-op when readline is not available at build time, or
 * when demo_name is NULL/empty.
 */
void cli_enable_history(cli_t* cli, const char* demo_name);

/**
 * @brief Run the CLI main loop (blocks until cli_quit() is called).
 *
 * Reads lines from stdin using readline, tokenises them, and dispatches
 * to registered handlers.  Unrecognised commands print a short error.
 *
 * The dashboard (if any) should be started before calling cli_run().
 */
void cli_run(cli_t* cli);

/**
 * @brief Signal the CLI loop to exit after the current command returns.
 *
 * Safe to call from a command handler.
 */
void cli_quit(cli_t* cli);

/**
 * @brief Print a message without disturbing the readline prompt.
 *
 * Use this (instead of printf) for async log output while the CLI is
 * running, so the prompt is cleanly reprinted below the message.
 */
void cli_print(cli_t* cli, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_DEMO_CLI_H_ */
