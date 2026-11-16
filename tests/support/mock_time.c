// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <time.h>

static time_t mock_time_value = 0;
static int    mock_active = 0;

void mock_time_set(time_t t) {
    mock_time_value = t;
    mock_active = 1;
}

void mock_time_clear(void) {
    mock_active = 0;
}

void mock_time_advance(time_t seconds) {
    mock_time_value += seconds;
}

time_t __wrap_time(time_t *t) {
    if (mock_active) {
        if (t) *t = mock_time_value;
        return mock_time_value;
    }
    extern time_t __real_time(time_t *t);
    return __real_time(t);
}
