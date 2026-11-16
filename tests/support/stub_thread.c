// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include "src/platform/eebus_thread.h"
#include <stdlib.h>

eebus_err_t eebus_thread_create(
    eebus_thread_t**  thread,
    eebus_thread_fn_t fn,
    void*             arg,
    const char*       name,
    size_t            stack_size
) {
  (void)thread; (void)fn; (void)arg; (void)name; (void)stack_size;
  return EEBUS_ERR_UNSUPPORTED;
}

eebus_err_t eebus_thread_join(eebus_thread_t* thread) {
  (void)thread;
  return EEBUS_ERR_UNSUPPORTED;
}

eebus_err_t eebus_thread_detach(eebus_thread_t* thread) {
  (void)thread;
  return EEBUS_ERR_UNSUPPORTED;
}

void eebus_thread_sleep_ms(unsigned int ms) {
  (void)ms;
}

bool eebus_thread_is_self(const eebus_thread_t* thread) {
  (void)thread;
  return true;
}
