// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/dashboard.h
 *
 * ANSI terminal status panel that refreshes periodically in a background
 * thread, while co-existing with the CLI readline prompt.
 *
 * The dashboard clears a fixed-height region at the top of the terminal,
 * redraws the status panel, then leaves the cursor at the bottom so the
 * CLI prompt stays visible below.
 *
 * Usage:
 *   dashboard_t* db = dashboard_create(3, render_fn, app_ctx);
 *   dashboard_start(db);
 *   cli_run(cli);          // blocks
 *   dashboard_stop(db);
 *   dashboard_destroy(db);
 */

#ifndef EEBUS_DEMO_DASHBOARD_H_
#define EEBUS_DEMO_DASHBOARD_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dashboard dashboard_t;

/**
 * @brief Dashboard refresh mode.
 *
 * MANUAL (default): panel only redraws when dashboard_refresh() is called
 *                   explicitly (e.g. from the "dashboard refresh" CLI command).
 * AUTO:             a background thread redraws the panel every interval_s
 *                   seconds; dashboard_refresh() still works for on-demand
 *                   redraws.
 */
typedef enum {
  DASHBOARD_MODE_MANUAL = 0, /**< Manual refresh only (default). */
  DASHBOARD_MODE_AUTO   = 1, /**< Automatic periodic refresh.    */
} dashboard_mode_t;

/**
 * @brief Render callback — called every interval_s seconds (AUTO) or on
 *        dashboard_refresh() (MANUAL).
 *
 * The callback should write the panel content using dashboard_row() /
 * dashboard_separator() / printf.  It must not block for long.
 *
 * @param userdata  Value passed to dashboard_create().
 */
typedef void (*dashboard_render_fn)(void* userdata);

/**
 * @brief Create a dashboard.
 *
 * @param mode         DASHBOARD_MODE_MANUAL or DASHBOARD_MODE_AUTO.
 * @param interval_s   Refresh interval in seconds (AUTO mode only; ignored
 *                     in MANUAL mode; minimum 1).
 * @param render_fn    Content renderer callback.
 * @param userdata     Forwarded to render_fn.
 * @return New dashboard_t, or NULL on allocation failure.
 */
dashboard_t* dashboard_create(
    dashboard_mode_t     mode,
    int                  interval_s,
    dashboard_render_fn  render_fn,
    void*                userdata);

/** Free dashboard resources.  Must call dashboard_stop() first. */
void dashboard_destroy(dashboard_t* db);

/**
 * @brief Start the dashboard.
 *
 * AUTO mode:   launches the background refresh thread.
 * MANUAL mode: reserves terminal space and does an initial render; no
 *              background thread is started.
 */
void dashboard_start(dashboard_t* db);

/**
 * @brief Stop the dashboard.
 *
 * AUTO mode:   stops the background thread (blocks until it exits).
 * MANUAL mode: no-op (no thread to stop).
 */
void dashboard_stop(dashboard_t* db);

/**
 * @brief Force an immediate redraw.
 *
 * Works in both MANUAL and AUTO modes.  Safe to call from any thread.
 */
void dashboard_refresh(dashboard_t* db);

/**
 * @brief Switch refresh mode at runtime.
 *
 * AUTO → MANUAL: stops the background thread.
 * MANUAL → AUTO: starts the background thread.
 * Same mode: no-op.
 *
 * @param db    Dashboard handle.
 * @param mode  New mode.
 */
void dashboard_set_mode(dashboard_t* db, dashboard_mode_t mode);

/* ── Helpers for render callbacks ────────────────────────────────────────── */

/** Print a full-width horizontal separator line. */
void dashboard_separator(void);

/**
 * @brief Print one status row.
 *
 * @param label  Left-hand label (padded to 20 chars).
 * @param fmt    printf-style format for the value.
 */
void dashboard_row(const char* label, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

/** Print a blank row. */
void dashboard_blank(void);

/** Print free-form text (one line) inside render callback. Auto-counted. */
void dashboard_printf(const char* fmt, ...)
    __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_DEMO_DASHBOARD_H_ */
