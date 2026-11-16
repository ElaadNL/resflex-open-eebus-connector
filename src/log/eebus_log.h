// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_log.h
 * @brief EEBus SDK logging subsystem.
 *
 * ## Features
 * - Five severity levels: DEBUG, INFO, WARN, ERROR, FATAL.
 * - Simultaneous output to console (stderr) and/or a log file.
 * - Automatic log-file rotation by:
 *     - **Size**:  rotate when the current file exceeds a configurable limit.
 *     - **Time**:  rotate at configurable wall-clock intervals (hourly / daily).
 *   Both policies may be active simultaneously; whichever threshold is hit
 *   first triggers a rotation.
 * - Rotation keeps at most N backup files; older ones are deleted.
 * - Thread-safe: all log functions may be called from any thread.
 *
 * ## Quick-start
 * @code
 * eebus_log_config_t cfg = EEBUS_LOG_CONFIG_DEFAULT;
 * cfg.min_level      = EEBUS_LOG_INFO;
 * cfg.file_path      = "/var/log/eebus/eebus.log";
 * cfg.max_file_bytes = 10 * 1024 * 1024;   // 10 MB
 * cfg.max_files      = 5;
 * cfg.rotate_seconds = 86400;              // daily
 *
 * eebus_log_init(&cfg);
 *
 * EEBUS_LOG_INFO("service", "EEBus SDK started, version %s", EEBUS_VERSION);
 * EEBUS_LOG_DEBUG("ship", "Sending Hello, phase=pending");
 *
 * eebus_log_deinit();
 * @endcode
 */

#ifndef EEBUS_INCLUDE_EEBUS_LOG_H_
#define EEBUS_INCLUDE_EEBUS_LOG_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include "../common/eebus_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Log levels ──────────────────────────────────────────────────────────── */

/**
 * @brief Log severity levels, in ascending order of importance.
 *
 * Only messages whose level is >= the configured minimum level are emitted.
 */
typedef enum {
  EEBUS_LOG_DEBUG = 0, /**< Detailed trace information for developers.      */
  EEBUS_LOG_INFO  = 1, /**< Normal operational messages.                    */
  EEBUS_LOG_WARN  = 2, /**< Unexpected situation that is not yet an error.  */
  EEBUS_LOG_ERROR = 3, /**< Recoverable error condition.                    */
  EEBUS_LOG_FATAL = 4, /**< Unrecoverable error; SDK will shut down.        */
} eebus_log_level_t;

/* ── Time-based rotation policy ──────────────────────────────────────────── */

/**
 * @brief Predefined rotation intervals.
 *
 * A custom interval in seconds may also be set directly via
 * eebus_log_config_t::rotate_seconds.
 */
#define EEBUS_LOG_ROTATE_NONE    (0U)        /**< No time-based rotation.   */
#define EEBUS_LOG_ROTATE_HOURLY  (3600U)     /**< Rotate every hour.        */
#define EEBUS_LOG_ROTATE_DAILY   (86400U)    /**< Rotate every 24 hours.    */

/* ── Configuration ───────────────────────────────────────────────────────── */

/**
 * @brief Logger configuration passed to eebus_log_init().
 *
 * All fields have sensible defaults available via EEBUS_LOG_CONFIG_DEFAULT.
 */
typedef struct {
  /**
   * @brief Minimum level to emit.
   *
   * Messages below this level are silently discarded.
   * Default: EEBUS_LOG_DEBUG (emit everything).
   */
  eebus_log_level_t min_level;

  /**
   * @brief Whether to print log messages to stderr.
   *
   * Colour-coded output is used when stderr is a TTY.
   * Default: true.
   */
  bool console_enabled;

  /**
   * @brief Path of the primary log file, or NULL to disable file logging.
   *
   * The directory must exist and be writable.  The logger does not create
   * intermediate directories.
   * Default: NULL (file logging disabled).
   *
   * Example: "/var/log/eebus/eebus.log"
   *
   * Rotated backups are named:
   *   eebus.log.1, eebus.log.2, … eebus.log.<max_files-1>
   * where .1 is the most recent backup.
   */
  const char* file_path;

  /**
   * @brief Maximum size in bytes before a size-triggered rotation occurs.
   *
   * Set to 0 to disable size-based rotation.
   * Default: 10 MiB (10 * 1024 * 1024).
   */
  size_t max_file_bytes;

  /**
   * @brief Number of backup files to keep (not counting the active file).
   *
   * When rotation occurs, backups are shifted: .1 → .2 → … → .N.
   * The oldest backup (.N) is deleted.
   * Minimum: 1.  Default: 5.
   */
  unsigned int max_files;

  /**
   * @brief Interval in seconds for time-triggered rotation.
   *
   * Use EEBUS_LOG_ROTATE_NONE (0) to disable time-based rotation.
   * Use EEBUS_LOG_ROTATE_HOURLY or EEBUS_LOG_ROTATE_DAILY for convenience.
   * Default: EEBUS_LOG_ROTATE_DAILY.
   */
  unsigned int rotate_seconds;

  /**
   * @brief Optional user-supplied callback invoked for every emitted record.
   *
   * Can be used to forward log records to an external system (syslog, MQTT,
   * …).  The callback is invoked synchronously from the calling thread while
   * the internal log mutex is held; it must not call any eebus_log_* function.
   *
   * @param level    Severity of the record.
   * @param tag      Module tag string (not NULL-terminated past the tag).
   * @param message  Fully formatted log line (without trailing newline).
   * @param userdata Value of eebus_log_config_t::callback_userdata.
   */
  void (*callback)(eebus_log_level_t level, const char* tag, const char* message, void* userdata);

  /** @brief Arbitrary pointer forwarded to every callback invocation. */
  void* callback_userdata;
} eebus_log_config_t;

/**
 * @brief Initialise eebus_log_config_t with safe defaults.
 *
 * Expands to a designated-initialiser expression.  Usage:
 * @code
 *   eebus_log_config_t cfg = EEBUS_LOG_CONFIG_DEFAULT;
 *   cfg.file_path = "/var/log/eebus.log";
 * @endcode
 */
#define EEBUS_LOG_CONFIG_DEFAULT                      \
  {                                                   \
    .min_level      = EEBUS_LOG_DEBUG,                \
    .console_enabled = true,                          \
    .file_path      = NULL,                           \
    .max_file_bytes = (10U * 1024U * 1024U),          \
    .max_files      = 5U,                             \
    .rotate_seconds = EEBUS_LOG_ROTATE_DAILY,         \
    .callback       = NULL,                           \
    .callback_userdata = NULL,                        \
  }

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the logging subsystem.
 *
 * Must be called before any EEBUS_LOG_* macro.  Calling it more than once
 * without an intervening eebus_log_deinit() returns EEBUS_ERR_EXISTS.
 *
 * @param[in] config  Logger configuration.  The pointed-to struct is copied
 *                    internally; the caller may free or reuse it afterwards.
 *                    Must not be NULL.
 * @return  EEBUS_OK on success, or an EEBUS_ERR_* code on failure.
 */
eebus_err_t eebus_log_init(const eebus_log_config_t* config);

/**
 * @brief Flush all pending log records and shut down the logging subsystem.
 *
 * After this call all EEBUS_LOG_* macros are no-ops until the next
 * eebus_log_init().  Safe to call even if eebus_log_init() was never called.
 */
void eebus_log_deinit(void);

/**
 * @brief Update the minimum log level at runtime.
 *
 * Thread-safe.  Useful for temporarily increasing verbosity during diagnosis.
 *
 * @param[in] level  New minimum level.
 */
void eebus_log_set_level(eebus_log_level_t level);

/**
 * @brief Query the current minimum log level.
 *
 * @return Current minimum level.
 */
eebus_log_level_t eebus_log_get_level(void);

/**
 * @brief Force an immediate log-file rotation.
 *
 * No-op if file logging is disabled.
 *
 * @return EEBUS_OK on success, or EEBUS_ERR_IO if the rotation failed.
 */
eebus_err_t eebus_log_rotate(void);

/* ── Core write function (used by macros) ────────────────────────────────── */

/**
 * @brief Write a log record.  Prefer the EEBUS_LOG_* macros over this function.
 *
 * @param[in] level   Severity level.
 * @param[in] tag     Short module identifier, e.g. "ship", "spine", "lpc".
 * @param[in] file    Source file name (__FILE__).
 * @param[in] line    Source line number (__LINE__).
 * @param[in] fmt     printf-style format string.
 * @param[in] ...     Format arguments.
 */
void eebus_log_write(
    eebus_log_level_t level,
    const char*       tag,
    const char*       file,
    int               line,
    const char*       fmt,
    ...
) __attribute__((format(printf, 5, 6)));

/* ── Log-injection sanitisation (CWE-117) ────────────────────────────────── */

/**
 * @brief Copy @p src into @p dst, neutralising control characters.
 *
 * Every C0 control byte (0x00–0x1F, incl. CR/LF/TAB/ESC) and DEL (0x7F) is
 * replaced with '_'.  Use this on untrusted, remotely-supplied strings
 * (e.g. SHIP/EEBUS node names, SKIs, mDNS-derived hostnames) before logging
 * them, to prevent log-forging / terminal-escape injection.
 *
 * The result is always NUL-terminated (unless @p n == 0).
 *
 * @param[out] dst  Destination buffer.
 * @param      n    Size of @p dst in bytes.
 * @param[in]  src  Source string (may be NULL → empty result).
 * @return          @p dst, so the call can be used directly as a printf arg.
 */
const char* eebus_log_sanitize(char* dst, size_t n, const char* src);

/* ── Convenience macros ──────────────────────────────────────────────────── */

/**
 * @defgroup log_macros Logging macros
 * @{
 *
 * Each macro accepts a @p tag string (module identifier) followed by a
 * printf-style format string and optional arguments.
 *
 * Example:
 * @code
 *   EEBUS_LOG_INFO("ship", "Connected to %s SKI=%s", hostname, ski);
 *   EEBUS_LOG_ERROR("spine", "Datagram parse failed: %s", eebus_err_str(err));
 * @endcode
 */

/** @brief Emit a DEBUG-level message.
 *  Usage: EEBUS_LOG_DEBUG(tag, fmt)  or  EEBUS_LOG_DEBUG(tag, fmt, arg, ...) */
#define EEBUS_LOG_DEBUG(tag, ...) \
  eebus_log_write(EEBUS_LOG_DEBUG, (tag), __FILE__, __LINE__, __VA_ARGS__)

/** @brief Emit an INFO-level message. */
#define EEBUS_LOG_INFO(tag, ...) \
  eebus_log_write(EEBUS_LOG_INFO,  (tag), __FILE__, __LINE__, __VA_ARGS__)

/** @brief Emit a WARN-level message. */
#define EEBUS_LOG_WARN(tag, ...) \
  eebus_log_write(EEBUS_LOG_WARN,  (tag), __FILE__, __LINE__, __VA_ARGS__)

/** @brief Emit an ERROR-level message. */
#define EEBUS_LOG_ERROR(tag, ...) \
  eebus_log_write(EEBUS_LOG_ERROR, (tag), __FILE__, __LINE__, __VA_ARGS__)

/** @brief Emit a FATAL-level message. */
#define EEBUS_LOG_FATAL(tag, ...) \
  eebus_log_write(EEBUS_LOG_FATAL, (tag), __FILE__, __LINE__, __VA_ARGS__)

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_INCLUDE_EEBUS_LOG_H_ */
