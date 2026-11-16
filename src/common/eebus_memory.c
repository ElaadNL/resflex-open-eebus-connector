// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_memory.c
 * @brief Memory allocation backend.
 *
 * The current implementation delegates entirely to the system malloc/free.
 * To replace the allocator (e.g. for a custom pool), modify the EEBUS_MALLOC
 * and EEBUS_FREE macros in eebus_memory.h and add the implementation here.
 */

#include "eebus_memory.h"

/* No runtime code required: all allocation functions are macros. */
