// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_thread_linux.c
 * @brief POSIX pthreads backend for eebus_thread.h.
 */

#define _GNU_SOURCE /* pthread_setname_np */

#include "../eebus_thread.h"
#include "../../common/eebus_memory.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

/** Maximum thread name length on Linux (including NUL). */
#define THREAD_NAME_MAX 16

/** Internal thread structure. */
struct eebus_thread {
  pthread_t           handle;
  eebus_thread_fn_t   user_fn;
  void*               user_arg;
  volatile bool       started;
  atomic_uint         refs;
};

/** Release one owner reference and free the wrapper at the last reference. */
static void thread_release(struct eebus_thread* thread) {
  if (atomic_fetch_sub_explicit(&thread->refs, 1U,
                                memory_order_acq_rel) == 1U) {
    EEBUS_FREE(thread);
  }
}

/**
 * @brief POSIX thread wrapper.
 *
 * pthread entry points must return void*; this shim calls the SDK-style
 * void(*)(void*) function and returns NULL.
 */
static void* thread_entry(void* arg) {
  struct eebus_thread* t = (struct eebus_thread*)arg;
  eebus_thread_fn_t fn = t->user_fn;
  void*             ud = t->user_arg;
  t->started = true;
  fn(ud);
  /* Release the worker reference after the entry point has returned. */
  thread_release(t);
  return NULL;
}

eebus_err_t eebus_thread_create(
    eebus_thread_t**  thread,
    eebus_thread_fn_t fn,
    void*             arg,
    const char*       name,
    size_t            stack_size
) {
  if (!thread || !fn) return EEBUS_ERR_INVALID;

  struct eebus_thread* t = EEBUS_CALLOC(1, sizeof(*t));
  if (!t) return EEBUS_ERR_NO_MEMORY;

  t->user_fn  = fn;
  t->user_arg = arg;
  /* One reference belongs to the caller and one to the running thread. */
  atomic_init(&t->refs, 2U);

  /* Configure thread attributes */
  pthread_attr_t attr;
  pthread_attr_init(&attr);

  if (stack_size > 0) {
    pthread_attr_setstacksize(&attr, stack_size);
  }

  int rc = pthread_create(&t->handle, &attr, thread_entry, t);
  pthread_attr_destroy(&attr);

  if (rc != 0) {
    EEBUS_FREE(t);
    return EEBUS_ERR_INTERNAL;
  }

  /* Set thread name for debuggability (best-effort, Linux-specific) */
  if (name && name[0] != '\0') {
    char tname[THREAD_NAME_MAX];
    snprintf(tname, sizeof(tname), "%s", name);
    pthread_setname_np(t->handle, tname);
  }

  *thread = t;
  return EEBUS_OK;
}

eebus_err_t eebus_thread_join(eebus_thread_t* thread) {
  if (!thread) return EEBUS_ERR_INVALID;

  int rc = pthread_join(thread->handle, NULL);
  if (rc != 0) return EEBUS_ERR_INTERNAL;

  /* Release the caller reference after the worker has exited. */
  thread_release(thread);
  return EEBUS_OK;
}

eebus_err_t eebus_thread_detach(eebus_thread_t* thread) {
  if (!thread) return EEBUS_ERR_INVALID;

  int rc = pthread_detach(thread->handle);
  /* Wait until thread_entry has copied fn/arg from the struct */
  while (!thread->started)
    sched_yield();
  if (rc != 0) return EEBUS_ERR_INTERNAL;

  /* The worker releases its own reference when the entry point returns. */
  thread_release(thread);
  return EEBUS_OK;
}

void eebus_thread_sleep_ms(unsigned int ms) {
  struct timespec ts = {
    .tv_sec  = (time_t)(ms / 1000U),
    .tv_nsec = (long)((ms % 1000U) * 1000000L),
  };
  /* Loop on EINTR to survive signal interruptions */
  while (nanosleep(&ts, &ts) == -1) {
    /* continue */
  }
}

bool eebus_thread_is_self(const eebus_thread_t* thread) {
  if (!thread) return false;
  return pthread_equal(pthread_self(), thread->handle) != 0;
}
