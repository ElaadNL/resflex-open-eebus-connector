// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_mutex.h
 * @brief Platform-agnostic recursive mutex abstraction.
 *
 * A recursive mutex is used throughout the SDK so that a thread holding the
 * lock can re-enter a locked section without deadlocking (e.g. a callback
 * that calls back into the SDK).
 */

#ifndef EEBUS_INCLUDE_EEBUS_MUTEX_H_
#define EEBUS_INCLUDE_EEBUS_MUTEX_H_

#include "../common/eebus_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque recursive mutex handle. */
typedef struct eebus_mutex eebus_mutex_t;

/**
 * @brief Allocate and initialise a new recursive mutex.
 *
 * @param[out] mutex  Receives the mutex handle on success.
 * @return EEBUS_OK, or EEBUS_ERR_NO_MEMORY / EEBUS_ERR_INTERNAL on failure.
 */
eebus_err_t eebus_mutex_create(eebus_mutex_t** mutex);

/**
 * @brief Lock the mutex; blocks until the lock is acquired.
 *
 * @param[in] mutex  Handle from eebus_mutex_create().
 */
void eebus_mutex_lock(eebus_mutex_t* mutex);

/**
 * @brief Unlock the mutex.
 *
 * Must be called from the thread that currently holds the lock.
 *
 * @param[in] mutex  Handle from eebus_mutex_create().
 */
void eebus_mutex_unlock(eebus_mutex_t* mutex);

/**
 * @brief Destroy the mutex and free its resources.
 *
 * The mutex must not be locked when this is called.
 *
 * @param[in] mutex  Handle to destroy.  Passing NULL is a no-op.
 */
void eebus_mutex_destroy(eebus_mutex_t* mutex);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_INCLUDE_EEBUS_MUTEX_H_ */
