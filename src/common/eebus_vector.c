// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_vector.c
 * @brief Generic resizable pointer array implementation.
 */

#include "eebus_vector.h"
#include "eebus_memory.h"

#include <string.h>

/** Initial allocation when the vector grows from empty. */
#define VECTOR_INIT_CAPACITY 8U

/**
 * @brief Grow the vector's internal buffer if needed.
 *
 * Doubles capacity on each reallocation.
 *
 * @param[in] v         Vector to grow.
 * @param[in] required  Minimum required capacity after growth.
 * @return EEBUS_OK, or EEBUS_ERR_NO_MEMORY.
 */
static eebus_err_t vector_grow(eebus_vector_t* v, size_t required) {
  if (v->capacity >= required) return EEBUS_OK;

  size_t new_cap = (v->capacity == 0) ? VECTOR_INIT_CAPACITY : v->capacity;
  while (new_cap < required) {
    new_cap *= 2;
  }

  void** new_data = EEBUS_REALLOC(v->data, new_cap * sizeof(void*));
  if (!new_data) return EEBUS_ERR_NO_MEMORY;

  v->data     = new_data;
  v->capacity = new_cap;
  return EEBUS_OK;
}

eebus_err_t eebus_vector_push(eebus_vector_t* v, void* item) {
  if (!v) return EEBUS_ERR_INVALID;

  eebus_err_t err = vector_grow(v, v->size + 1);
  if (err != EEBUS_OK) return err;

  v->data[v->size++] = item;
  return EEBUS_OK;
}

eebus_err_t eebus_vector_pop(eebus_vector_t* v, void** item) {
  if (!v) return EEBUS_ERR_INVALID;
  if (v->size == 0) {
    if (item) *item = NULL;
    return EEBUS_ERR_NOT_FOUND;
  }

  v->size--;
  if (item) *item = v->data[v->size];
  return EEBUS_OK;
}

eebus_err_t eebus_vector_remove(eebus_vector_t* v, size_t index) {
  if (!v) return EEBUS_ERR_INVALID;
  if (index >= v->size) return EEBUS_ERR_INVALID;

  /* Shift elements left to fill the gap */
  size_t move_count = v->size - index - 1;
  if (move_count > 0) {
    memmove(&v->data[index], &v->data[index + 1], move_count * sizeof(void*));
  }
  v->size--;
  return EEBUS_OK;
}

eebus_err_t eebus_vector_find(const eebus_vector_t* v, const void* item, size_t* index) {
  if (!v) return EEBUS_ERR_INVALID;

  for (size_t i = 0; i < v->size; i++) {
    if (v->data[i] == item) {
      if (index) *index = i;
      return EEBUS_OK;
    }
  }
  return EEBUS_ERR_NOT_FOUND;
}

void eebus_vector_clear(eebus_vector_t* v) {
  if (!v) return;
  v->size = 0;
}

void eebus_vector_free(eebus_vector_t* v) {
  if (!v) return;
  EEBUS_FREE(v->data);
  v->data     = NULL;
  v->size     = 0;
  v->capacity = 0;
}
