// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/dashboard.c
 */

#include "dashboard.h"
#include "eebus_memory.h"

#include "eebus_mutex.h"
#include "eebus_thread.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* ── ANSI escape codes ───────────────────────────────────────────────────── */

#define ANSI_HIDE_CURSOR   "\033[?25l"
#define ANSI_SHOW_CURSOR   "\033[?25h"
#define ANSI_CLEAR_SCREEN  "\033[2J\033[H"   /* clear screen + cursor home */
#define ANSI_BOLD          "\033[1m"
#define ANSI_RESET         "\033[0m"
#define ANSI_GREEN         "\033[32m"
#define ANSI_YELLOW        "\033[33m"
#define ANSI_RED           "\033[31m"
#define ANSI_CYAN          "\033[36m"

/* Dynamic panel width: query terminal columns each render, falling back
 * to 80 when not attached to a TTY (pipe / redirect / no ioctl support). */
static int panel_width(void) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 20)
    return (int)ws.ws_col;
  return 80;
}

/* ── Internal state ──────────────────────────────────────────────────────── */

struct dashboard {
  dashboard_mode_t     mode;
  int                  interval_s;
  dashboard_render_fn  render_fn;
  void*                userdata;

  eebus_thread_t*      thread;
  eebus_mutex_t*       mutex;
  int                  running;
};

/* ── Row-printing helpers (used inside render callbacks) ─────────────────── */

void dashboard_separator(void) {
  int w = panel_width();
  printf("  ");
  for (int i = 0; i < w - 2; i++) putchar('-');
  putchar('\n');
}

void dashboard_row(const char* label, const char* fmt, ...) {
  printf("  " ANSI_BOLD "%-22s" ANSI_RESET, label);
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  putchar('\n');
}

void dashboard_blank(void) {
  putchar('\n');
}

void dashboard_printf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

/* ── Panel draw ──────────────────────────────────────────────────────────── */

/* fullscreen == true  (AUTO refresh): hide the cursor, clear the whole screen
 *                      and redraw from the top-left — immune to anything else
 *                      written to stdout between frames.
 * fullscreen == false (startup / manual refresh): just append a fresh panel
 *                      below the current cursor, like ordinary command output
 *                      (scrolls down, leaves history intact). */
static void draw_panel(dashboard_t* db, bool fullscreen) {
  if (fullscreen)
    printf(ANSI_HIDE_CURSOR ANSI_CLEAR_SCREEN);
  else
    putchar('\n');

  int w = panel_width();

  /* Top border */
  printf(ANSI_CYAN "  ");
  for (int i = 0; i < w - 2; i++) putchar('=');
  printf(ANSI_RESET "\n");

  /* Render — render_fn calls dashboard_row / dashboard_separator /
   * dashboard_printf to emit the panel body. */
  db->render_fn(db->userdata);

  /* Bottom border */
  printf(ANSI_CYAN "  ");
  for (int i = 0; i < w - 2; i++) putchar('=');
  printf(ANSI_RESET "\n");

  fflush(stdout);
}

/* ── Background thread ───────────────────────────────────────────────────── */

static void dashboard_thread(void* arg) {
  dashboard_t* db = (dashboard_t*)arg;

  /* Initial render */
  eebus_mutex_lock(db->mutex);
  draw_panel(db, true);
  eebus_mutex_unlock(db->mutex);

  while (db->running) {
    /* Sleep interval_s seconds in 100ms chunks so we can exit quickly */
    for (int i = 0; i < db->interval_s * 10 && db->running; i++) {
      struct timespec ts = { 0, 100000000L };  /* 100 ms */
      nanosleep(&ts, NULL);
    }
    if (!db->running) break;

    eebus_mutex_lock(db->mutex);
    draw_panel(db, true);
    eebus_mutex_unlock(db->mutex);
  }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

dashboard_t* dashboard_create(
    dashboard_mode_t    mode,
    int                 interval_s,
    dashboard_render_fn render_fn,
    void*               userdata)
{
  if (!render_fn) return NULL;

  dashboard_t* db = EEBUS_CALLOC(1, sizeof(*db));
  if (!db) return NULL;

  db->mode        = mode;
  db->interval_s  = interval_s < 1 ? 1 : interval_s;
  db->render_fn   = render_fn;
  db->userdata    = userdata;
  db->running     = 0;

  eebus_mutex_create(&db->mutex);
  return db;
}

void dashboard_destroy(dashboard_t* db) {
  if (!db) return;
  eebus_mutex_destroy(db->mutex);
  EEBUS_FREE(db);
}

void dashboard_start(dashboard_t* db) {
  if (!db || db->running) return;

  if (db->mode == DASHBOARD_MODE_MANUAL) {
    /* Manual: append the first panel below current output, no clear. */
    eebus_mutex_lock(db->mutex);
    draw_panel(db, false);
    eebus_mutex_unlock(db->mutex);
    return;
  }

  /* AUTO: launch background thread (it clears the screen on each frame) */
  db->running = 1;
  eebus_thread_create(&db->thread, dashboard_thread, db, "dashboard", 0);
}

void dashboard_stop(dashboard_t* db) {
  if (!db || !db->running) return;
  db->running = 0;
  eebus_thread_join(db->thread);
  printf(ANSI_SHOW_CURSOR);
  fflush(stdout);
}

void dashboard_refresh(dashboard_t* db) {
  if (!db) return;
  /* Manual refresh: append a fresh snapshot, no screen clear. */
  eebus_mutex_lock(db->mutex);
  draw_panel(db, false);
  eebus_mutex_unlock(db->mutex);
}

void dashboard_set_mode(dashboard_t* db, dashboard_mode_t mode) {
  if (!db || db->mode == mode) return;

  if (db->mode == DASHBOARD_MODE_AUTO && mode == DASHBOARD_MODE_MANUAL) {
    /* Stop background thread, restore the cursor hidden during AUTO */
    db->running = 0;
    eebus_thread_join(db->thread);
    db->mode = DASHBOARD_MODE_MANUAL;
    printf(ANSI_SHOW_CURSOR);
    printf("[DASHBOARD] Auto-refresh off  (type 'monitor' to refresh once, 'monitor on' to resume)\n");
    fflush(stdout);
  } else if (db->mode == DASHBOARD_MODE_MANUAL && mode == DASHBOARD_MODE_AUTO) {
    /* Start background thread (its first frame clears the screen) */
    db->mode    = DASHBOARD_MODE_AUTO;
    db->running = 1;
    eebus_thread_create(&db->thread, dashboard_thread, db, "dashboard", 0);
  }
}
