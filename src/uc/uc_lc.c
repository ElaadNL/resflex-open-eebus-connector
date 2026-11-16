// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * uc_lc.c — Shared utilities for LoadControl UC (LPC/LPP).
 */

#include "uc_lc.h"
#include <string.h>
#include <time.h>

uint32_t uc_lc_parse_duration_s(const char* str) {
    if (!str || str[0] != 'P') return 0;
    const char* p = str + 1;
    uint32_t total = 0, val = 0;

    /* Optional days before 'T' */
    while (*p && *p != 'T') {
        if (*p >= '0' && *p <= '9') val = val * 10 + (*p - '0');
        else if (*p == 'D') { total += val * 86400; val = 0; }
        else return 0;
        p++;
    }
    if (*p == 'T') p++;

    /* Time part: H, M, S */
    while (*p) {
        if (*p >= '0' && *p <= '9') val = val * 10 + (*p - '0');
        else if (*p == 'H') { total += val * 3600; val = 0; }
        else if (*p == 'M') { total += val * 60; val = 0; }
        else if (*p == 'S') { total += val; val = 0; }
        else return 0;
        p++;
    }
    return total;
}

uint32_t uc_lc_parse_time_period_ms(const spine_time_period_t* tp) {
    if (!tp->end_time_present || tp->end_time[0] == '\0') return 0;

    /* ISO 8601 duration (e.g., "PT3600S", "PT30M", "PT1H30M") */
    if (tp->end_time[0] == 'P')
        return uc_lc_parse_duration_s(tp->end_time) * 1000;

    /* Absolute ISO timestamp (e.g., "2024-01-01T12:00:00Z") */
    struct tm tm_end;
    memset(&tm_end, 0, sizeof(tm_end));
    if (strptime(tp->end_time, "%Y-%m-%dT%H:%M:%SZ", &tm_end)) {
        time_t end_epoch = timegm(&tm_end);
        time_t now = time(NULL);
        if (end_epoch > now)
            return (uint32_t)(end_epoch - now) * 1000;
    }
    return 0;
}
