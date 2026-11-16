// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE   /* localtime_r, clock_gettime, isatty */

/**
 * @file  eebus_log.c
 * @brief EEBus logging subsystem implementation.
 *
 * Thread-safety model
 * -------------------
 * A single eebus_mutex (g_log.mutex) protects all mutable state.  It is
 * acquired for the full duration of eebus_log_write() so that multi-line
 * records from different threads never interleave.
 *
 * Rotation algorithm
 * ------------------
 * Two independent policies are checked on every write:
 *
 *   Size policy: if current file size + new record > max_file_bytes → rotate.
 *   Time policy: if now - last_rotation_time > rotate_seconds       → rotate.
 *
 * Rotation renames the live file as a backup:
 *   eebus.log → eebus.log.1
 *   eebus.log.1 → eebus.log.2
 *   …
 *   eebus.log.(N-1) → eebus.log.N  (then deleted on next rotation)
 */

#include "eebus_log.h"

#include <errno.h>
#include "../common/eebus_memory.h"
#include "../platform/eebus_mutex.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ── ANSI colour codes for console output ────────────────────────────────── */
#define ANSI_RESET  "\033[0m"
#define ANSI_GREY   "\033[90m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_RED    "\033[31m"
#define ANSI_BOLD_RED "\033[1;31m"

/* Maximum length of the live log file path (user-supplied). */
#define LOG_PATH_MAX (1024)

/* Maximum length of a backup file path: base path + "." + 10-digit uint + NUL.
 * Must be at least LOG_PATH_MAX + 12 to avoid -Wformat-truncation. */
#define LOG_BACKUP_PATH_MAX (LOG_PATH_MAX + 16)

/* ── Internal state ──────────────────────────────────────────────────────── */

/** All mutable logger state, protected by mutex. */
typedef struct {
  bool              initialised;
  eebus_mutex_t*    mutex;

  /* Configuration (copied from caller) */
  eebus_log_level_t min_level;
  bool              console_enabled;
  bool              console_is_tty;   /**< true when stderr is a real terminal */
  char              file_path[LOG_PATH_MAX];
  size_t            max_file_bytes;
  unsigned int      max_files;
  unsigned int      rotate_seconds;

  /* User callback */
  void (*callback)(eebus_log_level_t, const char*, const char*, void*);
  void* callback_userdata;

  /* File state */
  FILE*  file;           /**< Current log file handle, or NULL */
  size_t file_bytes;     /**< Bytes written to the current file */

  /* Time-rotation state */
  time_t last_rotation;  /**< Wall-clock time of last rotation (or init) */
} log_state_t;

/** Module-global singleton. */
static log_state_t g_log = {
  .initialised = false,
  .mutex       = NULL,
};

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/** @brief Map level enum to short uppercase label. */
static const char* level_label(eebus_log_level_t level) {
  switch (level) {
    case EEBUS_LOG_DEBUG: return "DEBUG";
    case EEBUS_LOG_INFO:  return "INFO ";
    case EEBUS_LOG_WARN:  return "WARN ";
    case EEBUS_LOG_ERROR: return "ERROR";
    case EEBUS_LOG_FATAL: return "FATAL";
    default:              return "?????";
  }
}

/** @brief Map level to ANSI colour prefix (empty string when colours disabled). */
static const char* level_colour(eebus_log_level_t level, bool use_colour) {
  if (!use_colour) return "";
  switch (level) {
    case EEBUS_LOG_DEBUG: return ANSI_GREY;
    case EEBUS_LOG_INFO:  return ANSI_CYAN;
    case EEBUS_LOG_WARN:  return ANSI_YELLOW;
    case EEBUS_LOG_ERROR: return ANSI_RED;
    case EEBUS_LOG_FATAL: return ANSI_BOLD_RED;
    default:              return "";
  }
}

/**
 * @brief Replace CR/LF/TAB, other C0 control bytes and DEL with '_', in place.
 *
 * Defence-in-depth against CWE-117: any untrusted data that reaches a log
 * record (via the caller's format arguments) is stripped of characters that
 * could forge log lines or inject terminal escape sequences.
 */
static void scrub_controls(char* s, size_t len) {
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || c == 0x7f) s[i] = '_';
  }
}

/**
 * @brief Get the current file size (in bytes) of an open FILE*.
 *
 * @param fp  Open file handle.
 * @return    File size, or 0 on error.
 */
static size_t file_size(FILE* fp) {
  if (!fp) return 0;
  struct stat st;
  if (fstat(fileno(fp), &st) != 0) return 0;
  return (size_t)st.st_size;
}

/**
 * @brief Build the path for backup number @p n.
 *
 * Backup numbering: 1 = most recent, max_files = oldest.
 *
 * @param[out] out   Output buffer.
 * @param       size  Size of output buffer.
 * @param       base  Base log file path.
 * @param       n     Backup index (>= 1).
 */
static void backup_path(char* out, size_t size, const char* base, unsigned int n) {
  snprintf(out, size, "%s.%u", base, n);
}

/**
 * @brief Rotate backup files: shift .1→.2, .2→.3, … then rename live→.1.
 *
 * Called with g_log.mutex already held.
 *
 * @return EEBUS_OK, or EEBUS_ERR_IO on rename/unlink failure.
 */
static eebus_err_t do_rotate_locked(void) {
  char src[LOG_BACKUP_PATH_MAX];
  char dst[LOG_BACKUP_PATH_MAX];

  /* Close the current file so we can rename it. */
  if (g_log.file) {
    fclose(g_log.file);
    g_log.file = NULL;
  }

  /* Delete the oldest backup if it exists. */
  backup_path(dst, sizeof(dst), g_log.file_path, g_log.max_files);
  (void)remove(dst); /* Ignore error: file may not exist */

  /* Shift existing backups: .N-1 → .N, …, .1 → .2 */
  for (unsigned int i = g_log.max_files - 1; i >= 1; i--) {
    backup_path(src, sizeof(src), g_log.file_path, i);
    backup_path(dst, sizeof(dst), g_log.file_path, i + 1);
    /* rename() is a no-op when src does not exist on most platforms */
    (void)rename(src, dst);
  }

  /* Rename the live file to .1 */
  if (rename(g_log.file_path, src) != 0 && errno != ENOENT) {
    /* Non-fatal: continue and open a fresh file */
  }
  /* Build backup path for slot 1 (already in src via the backup_path call
   * for i = max_files-1 if max_files >= 2; recalculate to be safe). */
  backup_path(src, sizeof(src), g_log.file_path, 1U);
  (void)rename(g_log.file_path, src);

  /* Open a new live file */
  g_log.file = fopen(g_log.file_path, "a");
  if (!g_log.file) return EEBUS_ERR_IO;

  g_log.file_bytes    = file_size(g_log.file);
  g_log.last_rotation = time(NULL);
  return EEBUS_OK;
}

/**
 * @brief Check rotation policies and rotate if needed.
 *
 * Called with g_log.mutex held and only when file logging is active.
 *
 * @param[in] incoming_bytes  Number of bytes about to be written.
 */
static void maybe_rotate_locked(size_t incoming_bytes) {
  bool need_rotate = false;

  /* Size policy */
  if (g_log.max_file_bytes > 0
      && (g_log.file_bytes + incoming_bytes) > g_log.max_file_bytes) {
    need_rotate = true;
  }

  /* Time policy */
  if (!need_rotate && g_log.rotate_seconds > 0) {
    time_t now    = time(NULL);
    time_t elapsed = now - g_log.last_rotation;
    if (elapsed >= (time_t)g_log.rotate_seconds) {
      need_rotate = true;
    }
  }

  if (need_rotate) {
    (void)do_rotate_locked();
  }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

eebus_err_t eebus_log_init(const eebus_log_config_t* config) {
  if (!config) return EEBUS_ERR_INVALID;

  /* Create mutex on first init (static g_log.mutex starts as NULL) */
  if (!g_log.mutex) {
    if (eebus_mutex_create(&g_log.mutex) != EEBUS_OK)
      return EEBUS_ERR_NO_MEMORY;
  }

  eebus_mutex_lock(g_log.mutex);

  if (g_log.initialised) {
    eebus_mutex_unlock(g_log.mutex);
    return EEBUS_ERR_EXISTS;
  }

  /* Copy configuration */
  g_log.min_level       = config->min_level;
  g_log.console_enabled = config->console_enabled;
  g_log.console_is_tty  = isatty(fileno(stderr)) != 0;
  g_log.max_file_bytes  = config->max_file_bytes;
  g_log.max_files       = (config->max_files >= 1) ? config->max_files : 1;
  g_log.rotate_seconds  = config->rotate_seconds;
  g_log.callback        = config->callback;
  g_log.callback_userdata = config->callback_userdata;

  /* File path */
  g_log.file_path[0] = '\0';
  if (config->file_path && config->file_path[0] != '\0') {
    snprintf(g_log.file_path, sizeof(g_log.file_path), "%s", config->file_path);

    g_log.file = fopen(g_log.file_path, "a");
    if (!g_log.file) {
      eebus_mutex_unlock(g_log.mutex);
      return EEBUS_ERR_IO;
    }
    g_log.file_bytes = file_size(g_log.file);
  }

  g_log.last_rotation = time(NULL);
  g_log.initialised   = true;

  eebus_mutex_unlock(g_log.mutex);
  return EEBUS_OK;
}

void eebus_log_deinit(void) {
  if (!g_log.mutex) return;

  eebus_mutex_lock(g_log.mutex);

  if (g_log.file) {
    fflush(g_log.file);
    fclose(g_log.file);
    g_log.file = NULL;
  }
  g_log.initialised = false;

  eebus_mutex_unlock(g_log.mutex);
  eebus_mutex_destroy(g_log.mutex);
  g_log.mutex = NULL;
}

void eebus_log_set_level(eebus_log_level_t level) {
  eebus_mutex_lock(g_log.mutex);
  g_log.min_level = level;
  eebus_mutex_unlock(g_log.mutex);
}

eebus_log_level_t eebus_log_get_level(void) {
  eebus_mutex_lock(g_log.mutex);
  eebus_log_level_t lvl = g_log.min_level;
  eebus_mutex_unlock(g_log.mutex);
  return lvl;
}

eebus_err_t eebus_log_rotate(void) {
  eebus_mutex_lock(g_log.mutex);

  if (!g_log.initialised || g_log.file_path[0] == '\0') {
    eebus_mutex_unlock(g_log.mutex);
    return EEBUS_OK; /* No file logging; treat as success */
  }

  eebus_err_t err = do_rotate_locked();
  eebus_mutex_unlock(g_log.mutex);
  return err;
}

void eebus_log_write(
    eebus_log_level_t level,
    const char*       tag,
    const char*       file,
    int               line,
    const char*       fmt,
    ...
) {
  /* Fast path: if not initialised or level below minimum, do nothing.
   * Read min_level without the mutex for performance; the worst case is
   * emitting one extra record during a concurrent level change. */
  if (!g_log.initialised || level < g_log.min_level) return;

  /* ── Format timestamp ─────────────────────────────────────────────────── */
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);

  struct tm tm_info;
  localtime_r(&ts.tv_sec, &tm_info);

  char time_buf[32];
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

  /* ── Format the caller's message (dynamic allocation) ────────────────── */
  char* msg_buf = NULL;
  va_list ap;
  va_start(ap, fmt);
  int msg_len = vasprintf(&msg_buf, fmt, ap);
  va_end(ap);
  if (msg_len < 0 || !msg_buf) return;

  /* CWE-117: neutralise control chars from (possibly untrusted) arguments.
   * The prefix fields (timestamp/level/tag/file/line) are trusted and are
   * assembled separately below, so scrubbing the message body is sufficient. */
  scrub_controls(msg_buf, (size_t)msg_len);

  /* ── Assemble the full log line (dynamic allocation) ────────────────── */
  const char* fname = strrchr(file, '/');
  fname = fname ? fname + 1 : file;

  char* line_buf = NULL;
  int line_len = asprintf(&line_buf,
      "%s.%03ld [%-5s] [%-12s] [%s:%d] %s",
      time_buf,
      ts.tv_nsec / 1000000L, /* milliseconds */
      level_label(level),
      tag,
      fname,
      line,
      msg_buf
  );
  EEBUS_FREE(msg_buf);
  if (line_len < 0 || !line_buf) return;

  /* ── Emit ─────────────────────────────────────────────────────────────── */
  eebus_mutex_lock(g_log.mutex);

  /* Console output */
  if (g_log.console_enabled) {
    const char* colour = level_colour(level, g_log.console_is_tty);
    const char* reset  = g_log.console_is_tty ? ANSI_RESET : "";
    fprintf(stderr, "%s%s%s\n", colour, line_buf, reset);
  }

  /* File output */
  if (g_log.file) {
    /* Check rotation policies before writing */
    maybe_rotate_locked((size_t)(line_len + 1)); /* +1 for newline */

    if (g_log.file) { /* File may have been re-opened by rotation */
      int written = fprintf(g_log.file, "%s\n", line_buf);
      if (written > 0) {
        g_log.file_bytes += (size_t)written;
      }
      /* Flush immediately so records are visible even after a crash */
      fflush(g_log.file);
    }
  }

  /* User callback */
  if (g_log.callback) {
    g_log.callback(level, tag, line_buf, g_log.callback_userdata);
  }

  eebus_mutex_unlock(g_log.mutex);
  EEBUS_FREE(line_buf);
}

const char* eebus_log_sanitize(char* dst, size_t n, const char* src) {
  if (!dst || n == 0) return dst;
  if (!src) { dst[0] = '\0'; return dst; }

  size_t i = 0;
  for (; src[i] != '\0' && i + 1 < n; i++) {
    unsigned char c = (unsigned char)src[i];
    dst[i] = (c < 0x20 || c == 0x7f) ? '_' : src[i];
  }
  dst[i] = '\0';
  return dst;
}
