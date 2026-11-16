// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include "src/platform/eebus_mutex.h"
#include <stdlib.h>
#include <string.h>

/* allocate a small dummy struct so callers get a non-NULL pointer */
eebus_err_t eebus_mutex_create(eebus_mutex_t** mutex) {
  if (!mutex) return EEBUS_ERR_INVALID;
  *mutex = (eebus_mutex_t*)malloc(1);
  if (!*mutex) return EEBUS_ERR_NO_MEMORY;
  return EEBUS_OK;
}

void eebus_mutex_lock(eebus_mutex_t* mutex) {
  (void)mutex;
}

void eebus_mutex_unlock(eebus_mutex_t* mutex) {
  (void)mutex;
}

void eebus_mutex_destroy(eebus_mutex_t* mutex) {
  free(mutex);
}
