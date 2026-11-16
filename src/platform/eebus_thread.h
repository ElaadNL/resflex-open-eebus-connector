// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_thread.h
 * @brief Platform-agnostic thread abstraction.
 *
 * Implementors provide a concrete backend by compiling one of:
 *   - src/platform/linux/eebus_thread_linux.c  (POSIX pthreads)
 *
 * Only one backend may be linked at a time.
 */

#ifndef EEBUS_INCLUDE_EEBUS_THREAD_H_
#define EEBUS_INCLUDE_EEBUS_THREAD_H_

#include <stdbool.h>
#include <stddef.h>

#include "../common/eebus_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque thread handle. */
typedef struct eebus_thread eebus_thread_t;

/**
 * @brief Thread entry-point function signature.
 *
 * @param arg  User argument supplied to eebus_thread_create().
 */
typedef void (*eebus_thread_fn_t)(void* arg);

/**
 * @brief Create and start a new thread.
 *
 * The thread begins executing @p fn(arg) immediately after creation.
 *
 * @param[out] thread      Receives the new thread handle on success.  The
 *                         handle must eventually be passed to eebus_thread_join()
 *                         or eebus_thread_detach().
 * @param[in]  fn          Entry-point function. Must not be NULL.
 * @param[in]  arg         Opaque argument forwarded to @p fn.
 * @param[in]  name        Optional human-readable thread name (may be NULL).
 *                         Truncated to the platform maximum (e.g. 15 chars on Linux).
 * @param[in]  stack_size  Stack size in bytes, or 0 for the platform default.
 * @return EEBUS_OK on success, EEBUS_ERR_* on failure.
 */
eebus_err_t eebus_thread_create(
    eebus_thread_t**  thread,
    eebus_thread_fn_t fn,
    void*             arg,
    const char*       name,
    size_t            stack_size
);

/**
 * @brief Block until the thread exits and release the caller's reference.
 *
 * Must not be called from the thread itself (deadlock).
 * The handle is released after a successful join.  The caller must not use
 * the handle or call eebus_thread_join() / eebus_thread_detach() again.
 *
 * @param[in] thread  Handle returned by eebus_thread_create().
 * @return EEBUS_OK, or EEBUS_ERR_INTERNAL if the join failed.
 */
eebus_err_t eebus_thread_join(eebus_thread_t* thread);

/**
 * @brief Detach a thread and release the caller's reference.
 *
 * The worker keeps its own reference while it is running and releases that
 * reference when the entry function returns.  The opaque thread wrapper is
 * therefore freed after the worker exits, rather than immediately when this
 * function returns.  The worker may continue to use the handle until its
 * entry function returns.  After a successful detach, the caller must not
 * use the handle or call eebus_thread_join() / eebus_thread_detach() again.
 *
 * @param[in] thread  Handle returned by eebus_thread_create().
 * @return EEBUS_OK, or EEBUS_ERR_INTERNAL on failure.
 */
eebus_err_t eebus_thread_detach(eebus_thread_t* thread);

/**
 * @brief Sleep the calling thread for at least @p ms milliseconds.
 *
 * @param[in] ms  Duration in milliseconds.
 */
void eebus_thread_sleep_ms(unsigned int ms);

/**
 * @brief Check if the calling thread is the given thread.
 *
 * @param thread  Thread handle from eebus_thread_create().  May be NULL.
 * @return true if the caller is running on @p thread, false otherwise.
 */
bool eebus_thread_is_self(const eebus_thread_t* thread);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_INCLUDE_EEBUS_THREAD_H_ */
