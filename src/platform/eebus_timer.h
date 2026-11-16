// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_timer.h
 * @brief Platform-agnostic one-shot and periodic software timer.
 *
 * Timers fire a callback from a dedicated timer thread.  The callback must
 * not block for extended periods or call eebus_timer_destroy() on itself
 * (doing so would deadlock).
 *
 * Typical usage (heartbeat watchdog):
 * @code
 *   eebus_timer_t* wdog = NULL;
 *   eebus_timer_create(&wdog, on_failsafe_timeout, my_ctx);
 *   eebus_timer_start(wdog, 60000, false);  // 60-second one-shot
 *
 *   // On heartbeat received:
 *   eebus_timer_restart(wdog, 60000);
 *
 *   // On teardown:
 *   eebus_timer_destroy(wdog);
 * @endcode
 */

#ifndef EEBUS_INCLUDE_EEBUS_TIMER_H_
#define EEBUS_INCLUDE_EEBUS_TIMER_H_

#include <stdbool.h>
#include <stdint.h>

#include "../common/eebus_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque timer handle. */
typedef struct eebus_timer eebus_timer_t;

/**
 * @brief Timer expiry callback.
 *
 * @param userdata  Value supplied to eebus_timer_create().
 */
typedef void (*eebus_timer_cb_t)(void* userdata);

/**
 * @brief Allocate a new timer (not yet started).
 *
 * @param[out] timer     Receives the timer handle on success.
 * @param[in]  callback  Function called when the timer expires.  Must not be NULL.
 * @param[in]  userdata  Forwarded to @p callback on each expiry.
 * @return EEBUS_OK, or EEBUS_ERR_* on failure.
 */
eebus_err_t eebus_timer_create(eebus_timer_t** timer, eebus_timer_cb_t callback, void* userdata);

/**
 * @brief Start or restart the timer.
 *
 * If the timer is already running it is restarted with the new interval;
 * the previous timeout is cancelled.
 *
 * @param[in] timer    Handle from eebus_timer_create().
 * @param[in] ms       Interval in milliseconds.  Must be > 0.
 * @param[in] periodic If true, the timer fires repeatedly every @p ms
 *                     milliseconds.  If false, it fires once (one-shot).
 * @return EEBUS_OK, or EEBUS_ERR_* on failure.
 */
eebus_err_t eebus_timer_start(eebus_timer_t* timer, unsigned int ms, bool periodic);

/**
 * @brief Stop a running timer without destroying it.
 *
 * If the timer is not running, this is a no-op.
 *
 * @param[in] timer  Handle from eebus_timer_create().
 */
void eebus_timer_stop(eebus_timer_t* timer);

/**
 * @brief Restart the timer with the same interval as the last eebus_timer_start() call.
 *
 * Convenience wrapper for watchdog patterns where the interval is fixed but
 * the timer needs to be reset on each received heartbeat.
 *
 * @param[in] timer  Handle from eebus_timer_create().\n * @return EEBUS_OK, or EEBUS_ERR_STATE if the timer was never started.
 */
eebus_err_t eebus_timer_restart(eebus_timer_t* timer);

/**
 * @brief Return the remaining time on a running timer, in milliseconds.
 *
 * Used by the SHIP Hello phase to populate the "waiting" sub-element
 * (SHIP §13.4.4.1) so that the peer can compute its Send-Prolongation-
 * Request-Timer correctly.
 *
 * @param[in] timer  Handle from eebus_timer_create().  May be NULL.
 * @return Remaining milliseconds, or 0 if the timer is not running / NULL.
 */
uint32_t eebus_timer_remaining_ms(const eebus_timer_t* timer);

/**
 * @brief Return the interval (ms) used in the last eebus_timer_start() call.
 * @param[in] timer  Handle from eebus_timer_create().  May be NULL.
 * @return Last interval in ms, or 0 if never started / NULL.
 */
uint32_t eebus_timer_last_ms(const eebus_timer_t* timer);

/**
 * @brief Stop the timer, free all resources, and invalidate the handle.
 *
 * The pointer @p timer must not be used after this call.
 * Passing NULL is a no-op.
 *
 * @param[in] timer  Handle to destroy.
 */
void eebus_timer_destroy(eebus_timer_t* timer);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_INCLUDE_EEBUS_TIMER_H_ */
