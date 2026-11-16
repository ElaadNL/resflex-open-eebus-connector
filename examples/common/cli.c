// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/cli.c
 */

#include "cli.h"
#include "eebus_memory.h"
#include "eebus_error.h"
#include "tls_persist.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Use readline if available, otherwise fall back to fgets */
#ifdef HAVE_READLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

/* ── Constants ───────────────────────────────────────────────────────────── */

#define CLI_MAX_COMMANDS  64
#define CLI_MAX_TOKENS    16
#define CLI_PROMPT_MAX    32
#define CLI_LINE_MAX      512

/* ── Internal types ──────────────────────────────────────────────────────── */

typedef struct {
  char           name[32];
  cli_handler_fn handler;
  char           help[128];
  void*          userdata;
} cli_cmd_t;

struct cli {
  char         prompt[CLI_PROMPT_MAX];
  cli_cmd_t    cmds[CLI_MAX_COMMANDS];
  int          ncmds;
  int          running;
  cli_blank_fn blank_handler;
  void*        blank_ud;
  char         history_path[PATH_MAX];   /* "" = history disabled */
};

/* ── History path / dir helpers (mirrors cs_config.c pattern) ─────────────── */

#ifdef HAVE_READLINE

#define CLI_HISTORY_FILE "history"

static bool build_path(const char* name, char* out, size_t outsz) {
  char dir[PATH_MAX];
  if (tls_persist_dir(name, dir, sizeof(dir)) != EEBUS_OK) return false;
  int n = snprintf(out, outsz, "%s/" CLI_HISTORY_FILE, dir);
  return n > 0 && (size_t)n < outsz;
}

static bool mkdirs(const char* path) {
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s", path);
  size_t len = strlen(tmp);
  if (len && tmp[len - 1] == '/') tmp[len - 1] = '\0';
  for (char* p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return false;
      *p = '/';
    }
  }
  if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return false;
  return true;
}

static bool ensure_dir(const char* name) {
  char dir[PATH_MAX];
  if (tls_persist_dir(name, dir, sizeof(dir)) != EEBUS_OK) return false;
  return mkdirs(dir);
}

#endif /* HAVE_READLINE */

/* ── Built-in: help ──────────────────────────────────────────────────────── */

static void builtin_help(int argc, char** argv, void* userdata) {
  (void)argc; (void)argv;
  cli_t* cli = (cli_t*)userdata;
  printf("\nAvailable commands:\n");
  for (int i = 0; i < cli->ncmds; i++) {
    printf("  %-24s  %s\n", cli->cmds[i].name, cli->cmds[i].help);
  }
  printf("\n");
}

static void builtin_clear(int argc, char** argv, void* userdata) {
  (void)argc; (void)argv; (void)userdata;
  /* \033[2J clear screen, \033[H cursor home */
  printf("\033[2J\033[H");
  fflush(stdout);
}

/* ── Tokeniser ───────────────────────────────────────────────────────────── */

static int tokenise(char* line, char** argv, int max_argc) {
  int argc = 0;
  char* p = line;
  while (*p && argc < max_argc) {
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) break;
    argv[argc++] = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = '\0';
  }
  return argc;
}

/* ── Line reading ────────────────────────────────────────────────────────── */

static char* read_line(const char* prompt) {
#ifdef HAVE_READLINE
  char* line = readline(prompt);
  if (line && *line) add_history(line);
  return line;  /* caller must free() */
#else
  printf("%s", prompt);
  fflush(stdout);
  char* buf = EEBUS_MALLOC(CLI_LINE_MAX);
  if (!buf) return NULL;
  if (!fgets(buf, CLI_LINE_MAX, stdin)) {
    EEBUS_FREE(buf);
    return NULL;
  }
  /* Strip trailing newline */
  size_t len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
  return buf;  /* caller must free() */
#endif
}

/* ── Public API ──────────────────────────────────────────────────────────── */

cli_t* cli_create(const char* prompt) {
  cli_t* cli = EEBUS_CALLOC(1, sizeof(*cli));
  if (!cli) return NULL;

  snprintf(cli->prompt, sizeof(cli->prompt), "[%s] > ", prompt ? prompt : "");
  cli->ncmds   = 0;
  cli->running = 0;

  /* Built-in help */
  cli_cmd_t* h = &cli->cmds[cli->ncmds++];
  snprintf(h->name, sizeof(h->name), "help");
  h->handler  = builtin_help;
  snprintf(h->help, sizeof(h->help), "Show this help");
  h->userdata = cli;

  /* Built-in clear */
  cli_cmd_t* c = &cli->cmds[cli->ncmds++];
  snprintf(c->name, sizeof(c->name), "clear");
  c->handler  = builtin_clear;
  snprintf(c->help, sizeof(c->help), "Clear the screen");
  c->userdata = NULL;

  return cli;
}

void cli_destroy(cli_t* cli) {
  EEBUS_FREE(cli);
}

int cli_register(
    cli_t*         cli,
    const char*    name,
    cli_handler_fn handler,
    const char*    help,
    void*          userdata)
{
  if (!cli || !name || !handler) return -1;
  if (cli->ncmds >= CLI_MAX_COMMANDS) return -1;

  cli_cmd_t* c = &cli->cmds[cli->ncmds++];
  snprintf(c->name, sizeof(c->name), "%s", name);
  snprintf(c->help, sizeof(c->help), "%s", help ? help : "");
  c->handler  = handler;
  c->userdata = userdata;
  return 0;
}

void cli_set_blank_handler(cli_t* cli, cli_blank_fn handler, void* userdata) {
  if (!cli) return;
  cli->blank_handler = handler;
  cli->blank_ud      = userdata;
}

void cli_enable_history(cli_t* cli, const char* demo_name) {
  if (!cli || !demo_name || !demo_name[0]) return;
#ifdef HAVE_READLINE
  if (!ensure_dir(demo_name)) return;
  (void)build_path(demo_name, cli->history_path, sizeof(cli->history_path));
#else
  (void)cli; (void)demo_name;
#endif
}

void cli_run(cli_t* cli) {
  if (!cli) return;
#ifdef HAVE_READLINE
  if (cli->history_path[0]) read_history(cli->history_path);
#endif
  cli->running = 1;

  while (cli->running) {
    char* line = read_line(cli->prompt);
    if (!line) {
      printf("\n");
      break;
    }

    /* Blank line: invoke handler (if registered) and continue. */
    char* trimmed = line;
    while (isspace((unsigned char)*trimmed)) trimmed++;
    if (!*trimmed) {
      if (cli->blank_handler) cli->blank_handler(cli->blank_ud);
      EEBUS_FREE(line);
      continue;
    }

    /* Tokenise into a mutable copy */
    char buf[CLI_LINE_MAX];
    snprintf(buf, sizeof(buf), "%s", trimmed);

    char* argv[CLI_MAX_TOKENS];
    int   argc = tokenise(buf, argv, CLI_MAX_TOKENS);
    EEBUS_FREE(line);
    if (argc == 0) continue;

    /* Dispatch */
    int found = 0;
    for (int i = 0; i < cli->ncmds; i++) {
      if (strcmp(argv[0], cli->cmds[i].name) == 0) {
        cli->cmds[i].handler(argc, argv, cli->cmds[i].userdata);
        found = 1;
        break;
      }
    }
    if (!found) {
      printf("Unknown command '%s'  (type 'help')\n", argv[0]);
    }
  }

#ifdef HAVE_READLINE
  if (cli->history_path[0]) {
    stifle_history(1000);
    write_history(cli->history_path);
  }
#endif
}

void cli_quit(cli_t* cli) {
  if (cli) cli->running = 0;
}

void cli_print(cli_t* cli, const char* fmt, ...) {
  (void)cli;

#ifdef HAVE_READLINE
  /* Clear readline's current input line so the message starts at col 0. */
  printf("\r\033[K");
#endif

  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  fflush(stdout);

#ifdef HAVE_READLINE
  /* Ask readline to redraw the prompt + any half-typed input. Safe to
   * call from a non-readline thread while readline is blocked on stdin
   * — the state isn't being mutated concurrently. */
  rl_forced_update_display();
#endif
}

