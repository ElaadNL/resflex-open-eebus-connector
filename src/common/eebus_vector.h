// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_vector.h
 * @brief Generic resizable array (vector) for pointer-sized elements.
 *
 * Elements are stored as void* pointers; the vector does not own the objects
 * they point to.  Callers are responsible for freeing element memory before
 * or after clearing/destroying the vector.
 *
 * Growth factor: capacity doubles on each reallocation.
 */

#ifndef EEBUS_INCLUDE_EEBUS_VECTOR_H_
#define EEBUS_INCLUDE_EEBUS_VECTOR_H_

#include <stddef.h>

#include "eebus_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Dynamic pointer array. */
typedef struct {
  void**  data;     /**< Pointer to element array (may be NULL when empty). */
  size_t  size;     /**< Number of elements currently stored.               */
  size_t  capacity; /**< Number of element slots allocated.                 */
} eebus_vector_t;

/** @brief Initialise an empty vector (stack-allocated or embedded). */
#define EEBUS_VECTOR_INIT { .data = NULL, .size = 0, .capacity = 0 }

/**
 * @brief Append a pointer to the end of the vector.
 *
 * Reallocates the internal buffer if necessary.
 *
 * @param[in] v      Vector to append to. Must not be NULL.
 * @param[in] item   Pointer value to append (may be NULL).
 * @return EEBUS_OK, or EEBUS_ERR_NO_MEMORY if reallocation fails.
 */
eebus_err_t eebus_vector_push(eebus_vector_t* v, void* item);

/**
 * @brief Remove and return the last element.
 *
 * @param[in]  v     Vector. Must not be NULL.
 * @param[out] item  Receives the removed pointer, or NULL if the vector
 *                   was empty.
 * @return EEBUS_OK, or EEBUS_ERR_NOT_FOUND if the vector is empty.
 */
eebus_err_t eebus_vector_pop(eebus_vector_t* v, void** item);

/**
 * @brief Remove the element at position @p index by shifting subsequent elements.
 *
 * @param[in] v      Vector. Must not be NULL.
 * @param[in] index  Zero-based index. Must be < v->size.
 * @return EEBUS_OK, or EEBUS_ERR_INVALID if index is out of range.
 */
eebus_err_t eebus_vector_remove(eebus_vector_t* v, size_t index);

/**
 * @brief Find the first occurrence of @p item (by pointer equality).
 *
 * @param[in]  v      Vector.
 * @param[in]  item   Pointer to search for.
 * @param[out] index  Set to the index of the found element (may be NULL).
 * @return EEBUS_OK if found, EEBUS_ERR_NOT_FOUND otherwise.
 */
eebus_err_t eebus_vector_find(const eebus_vector_t* v, const void* item, size_t* index);

/**
 * @brief Remove all elements without freeing the underlying array buffer.
 *
 * @param[in] v  Vector. Must not be NULL.
 */
void eebus_vector_clear(eebus_vector_t* v);

/**
 * @brief Free the internal buffer and reset the vector to an empty state.
 *
 * Does not free the elements; only the storage array is freed.
 *
 * @param[in] v  Vector. Must not be NULL.
 */
void eebus_vector_free(eebus_vector_t* v);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_INCLUDE_EEBUS_VECTOR_H_ */
