// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_timer_linux.c
 * @brief Linux timer backend using POSIX timer_create() with SIGEV_THREAD.
 *
 * Each timer spawns a kernel-managed thread on expiry (via SIGEV_THREAD).
 * This avoids a dedicated timer-management thread and keeps the implementation
 * simple.  The callback runs in a short-lived detached thread; it must not
 * block indefinitely.
 */

#define _POSIX_C_SOURCE 200809L

#include "../eebus_timer.h"
#include "../../common/eebus_memory.h"

#include <signal.h>
#include <string.h>
#include <time.h>

struct eebus_timer {
  timer_t          id;          /**< POSIX timer identifier                  */
  eebus_timer_cb_t callback;    /**< User callback                           */
  void*            userdata;    /**< Forwarded to callback                   */
  unsigned int     last_ms;     /**< Interval used in the last start() call  */
  bool             periodic;    /**< Whether the timer repeats               */
  bool             created;     /**< True once timer_create() succeeded      */
};

/**
 * @brief SIGEV_THREAD notification function.
 *
 * Called by the kernel in a new thread on each timer expiry.
 */
static void timer_notify(union sigval sv) {
  struct eebus_timer* t = (struct eebus_timer*)sv.sival_ptr;
  if (t && t->callback) {
    t->callback(t->userdata);
  }
}

eebus_err_t eebus_timer_create(eebus_timer_t** timer, eebus_timer_cb_t callback, void* userdata) {
  if (!timer || !callback) return EEBUS_ERR_INVALID;

  struct eebus_timer* t = EEBUS_CALLOC(1, sizeof(*t));
  if (!t) return EEBUS_ERR_NO_MEMORY;

  t->callback = callback;
  t->userdata = userdata;

  /* Configure SIGEV_THREAD: kernel spawns a thread on expiry */
  struct sigevent sev;
  memset(&sev, 0, sizeof(sev));
  sev.sigev_notify            = SIGEV_THREAD;
  sev.sigev_notify_function   = timer_notify;
  sev.sigev_value.sival_ptr   = t;

  if (timer_create(CLOCK_MONOTONIC, &sev, &t->id) != 0) {
    EEBUS_FREE(t);
    return EEBUS_ERR_INTERNAL;
  }

  t->created = true;
  *timer     = t;
  return EEBUS_OK;
}

eebus_err_t eebus_timer_start(eebus_timer_t* timer, unsigned int ms, bool periodic) {
  if (!timer || ms == 0) return EEBUS_ERR_INVALID;
  if (!timer->created)   return EEBUS_ERR_STATE;

  timer->last_ms  = ms;
  timer->periodic = periodic;

  long ns = (long)(ms % 1000U) * 1000000L;
  time_t sec = (time_t)(ms / 1000U);

  struct itimerspec its;
  its.it_value.tv_sec     = sec;
  its.it_value.tv_nsec    = ns;

  /* For periodic timers, set the interval; for one-shot, leave it zero */
  if (periodic) {
    its.it_interval.tv_sec  = sec;
    its.it_interval.tv_nsec = ns;
  } else {
    its.it_interval.tv_sec  = 0;
    its.it_interval.tv_nsec = 0;
  }

  if (timer_settime(timer->id, 0, &its, NULL) != 0) {
    return EEBUS_ERR_INTERNAL;
  }
  return EEBUS_OK;
}

void eebus_timer_stop(eebus_timer_t* timer) {
  if (!timer || !timer->created) return;

  struct itimerspec its;
  memset(&its, 0, sizeof(its)); /* All-zero disarms the timer */
  timer_settime(timer->id, 0, &its, NULL);
}

eebus_err_t eebus_timer_restart(eebus_timer_t* timer) {
  if (!timer) return EEBUS_ERR_INVALID;
  if (timer->last_ms == 0) return EEBUS_ERR_STATE;
  return eebus_timer_start(timer, timer->last_ms, timer->periodic);
}

uint32_t eebus_timer_remaining_ms(const eebus_timer_t* timer) {
  if (!timer || !timer->created) return 0;

  struct itimerspec its;
  if (timer_gettime(timer->id, &its) != 0) return 0;

  /* it_value is all-zero when the timer is disarmed */
  if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0) return 0;

  uint32_t ms = (uint32_t)(its.it_value.tv_sec * 1000)
              + (uint32_t)(its.it_value.tv_nsec / 1000000);
  return ms;
}

uint32_t eebus_timer_last_ms(const eebus_timer_t* timer) {
  if (!timer) return 0;
  return timer->last_ms;
}

void eebus_timer_destroy(eebus_timer_t* timer) {
  if (!timer) return;
  if (timer->created) {
    timer_delete(timer->id);
  }
  EEBUS_FREE(timer);
}
