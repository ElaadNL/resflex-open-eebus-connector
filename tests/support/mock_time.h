// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#ifndef MOCK_TIME_H_
#define MOCK_TIME_H_

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

void mock_time_set(time_t t);
void mock_time_clear(void);
void mock_time_advance(time_t seconds);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_TIME_H_ */
