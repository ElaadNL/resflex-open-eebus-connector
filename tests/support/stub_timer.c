// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include "src/platform/eebus_timer.h"
#include <stdlib.h>
#include <string.h>

#define STUB_TIMER_MAX 32

struct eebus_timer {
  int slot;
};

typedef struct {
  eebus_timer_cb_t cb;
  void*            ud;
  bool             valid;
} stub_entry_t;

static stub_entry_t g_slots[STUB_TIMER_MAX];
static int g_creation_order[STUB_TIMER_MAX]; /* slot index by creation order */
static int g_creation_count;

uint32_t g_stub_timer_remaining_ms;
uint32_t g_stub_timer_last_ms;

eebus_err_t eebus_timer_create(eebus_timer_t** timer,
                                eebus_timer_cb_t callback, void* userdata) {
  if (!timer) return EEBUS_ERR_INVALID;
  *timer = (eebus_timer_t*)malloc(sizeof(eebus_timer_t));
  if (!*timer) return EEBUS_ERR_NO_MEMORY;
  (*timer)->slot = -1;
  if (callback) {
    for (int i = 0; i < STUB_TIMER_MAX; i++) {
      if (!g_slots[i].valid) {
        g_slots[i].cb    = callback;
        g_slots[i].ud    = userdata;
        g_slots[i].valid = true;
        (*timer)->slot   = i;
        if (g_creation_count < STUB_TIMER_MAX)
          g_creation_order[g_creation_count++] = i;
        break;
      }
    }
  }
  return EEBUS_OK;
}

eebus_err_t eebus_timer_start(eebus_timer_t* timer, unsigned int ms,
                               bool periodic) {
  (void)timer;
  (void)ms;
  (void)periodic;
  return EEBUS_OK;
}

void eebus_timer_stop(eebus_timer_t* timer) {
  (void)timer;
}

eebus_err_t eebus_timer_restart(eebus_timer_t* timer) {
  (void)timer;
  return EEBUS_OK;
}

uint32_t eebus_timer_remaining_ms(const eebus_timer_t* timer) {
  (void)timer;
  return g_stub_timer_remaining_ms;
}

uint32_t eebus_timer_last_ms(const eebus_timer_t* timer) {
  (void)timer;
  return g_stub_timer_last_ms;
}

void eebus_timer_destroy(eebus_timer_t* timer) {
  if (timer) {
    if (timer->slot >= 0 && timer->slot < STUB_TIMER_MAX)
      g_slots[timer->slot].valid = false;
    free(timer);
  }
}

/* Fire the nth timer created (0-indexed by creation order). */
void stub_timer_fire_nth(int n) {
  if (n < 0 || n >= g_creation_count) return;
  int slot = g_creation_order[n];
  if (slot >= 0 && slot < STUB_TIMER_MAX &&
      g_slots[slot].valid && g_slots[slot].cb)
    g_slots[slot].cb(g_slots[slot].ud);
}

/* Fire the most recently created timer. */
void stub_timer_fire_last(void) {
  if (g_creation_count > 0)
    stub_timer_fire_nth(g_creation_count - 1);
}

/* Reset the timer registry (call in test SetUp). */
void stub_timer_reset(void) {
  memset(g_slots, 0, sizeof(g_slots));
  g_creation_count = 0;
  g_stub_timer_remaining_ms = 0;
  g_stub_timer_last_ms = 0;
}
