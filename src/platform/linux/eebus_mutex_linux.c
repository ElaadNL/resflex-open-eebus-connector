// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_mutex_linux.c
 * @brief POSIX pthreads recursive mutex backend.
 */

#define _GNU_SOURCE

#include "../eebus_mutex.h"
#include "../../common/eebus_memory.h"

#include <pthread.h>

struct eebus_mutex {
  pthread_mutex_t handle;
};

eebus_err_t eebus_mutex_create(eebus_mutex_t** mutex) {
  if (!mutex) return EEBUS_ERR_INVALID;

  struct eebus_mutex* m = EEBUS_CALLOC(1, sizeof(*m));
  if (!m) return EEBUS_ERR_NO_MEMORY;

  /* Use a recursive mutex so that a thread can lock it multiple times */
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

  int rc = pthread_mutex_init(&m->handle, &attr);
  pthread_mutexattr_destroy(&attr);

  if (rc != 0) {
    EEBUS_FREE(m);
    return EEBUS_ERR_INTERNAL;
  }

  *mutex = m;
  return EEBUS_OK;
}

void eebus_mutex_lock(eebus_mutex_t* mutex) {
  if (!mutex) return;
  pthread_mutex_lock(&mutex->handle);
}

void eebus_mutex_unlock(eebus_mutex_t* mutex) {
  if (!mutex) return;
  pthread_mutex_unlock(&mutex->handle);
}

void eebus_mutex_destroy(eebus_mutex_t* mutex) {
  if (!mutex) return;
  pthread_mutex_destroy(&mutex->handle);
  EEBUS_FREE(mutex);
}
