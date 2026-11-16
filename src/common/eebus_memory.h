// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_memory.h
 * @brief Memory allocation wrappers used throughout the SDK.
 *
 * All SDK internal allocations go through these macros so that the
 * allocator can be replaced (e.g. for embedded environments that need a
 * custom heap or a memory pool) by modifying a single translation unit.
 */

#ifndef EEBUS_INCLUDE_EEBUS_MEMORY_H_
#define EEBUS_INCLUDE_EEBUS_MEMORY_H_

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocate @p size bytes.  Returns NULL on failure.
 *
 * Equivalent to malloc(size).
 */
#define EEBUS_MALLOC(size) malloc(size)

/**
 * @brief Allocate @p count elements of @p size bytes each, zero-initialised.
 *
 * Equivalent to calloc(count, size).
 */
#define EEBUS_CALLOC(count, size) calloc((count), (size))

/**
 * @brief Resize a previously allocated block.
 *
 * Equivalent to realloc(ptr, size).
 */
#define EEBUS_REALLOC(ptr, size) realloc((ptr), (size))

/**
 * @brief Free a block allocated by EEBUS_MALLOC / EEBUS_CALLOC / EEBUS_REALLOC.
 *
 * Safe to call with NULL.
 */
#define EEBUS_FREE(ptr) free(ptr)

/**
 * @brief Zero-allocate a single struct and return a typed pointer.
 *
 * Example:
 * @code
 *   my_struct_t* s = EEBUS_NEW(my_struct_t);
 *   if (!s) return EEBUS_ERR_NO_MEMORY;
 * @endcode
 */
#define EEBUS_NEW(type) ((type*)calloc(1, sizeof(type)))

/**
 * @brief Free and NULL-out a pointer.
 *
 * Prevents accidental use-after-free by zeroing the pointer variable.
 *
 * @param ptr_var  An lvalue of pointer type.
 */
#define EEBUS_FREE_CLEAR(ptr_var) \
  do {                           \
    free(ptr_var);               \
    (ptr_var) = NULL;            \
  } while (0)

/**
 * @brief Duplicate a NUL-terminated string using EEBUS_MALLOC.
 *
 * Returns NULL if @p s is NULL or on allocation failure.
 * Caller must free the result with EEBUS_FREE().
 */
static inline char* eebus_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)EEBUS_MALLOC(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_INCLUDE_EEBUS_MEMORY_H_ */
