// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_router.c
 *
 * SPINE message router.
 *
 * dispatch: All messages from peer_ski are routed directly to the corresponding remote device;
 *           spine_device_dispatch handles decoding and handler invocation.
 * send: Routes directly using dev->ski, no need to distinguish local/remote.
 */

#include "spine_router.h"
#include "spine_device_internal.h"
#include "../log/eebus_log.h"
#include "../common/eebus_memory.h"

#include <string.h>

#define LOG_TAG "spine.router"

/* ── Internal structure ───────────────────────────────────────────────────── */

struct spine_router {
    spine_device_t*  local_dev;
    spine_device_t*  remote_devs[SPINE_ROUTER_PEER_MAX];
    uint8_t          remote_count;
    spine_send_fn_t  send_fn;
    void*            send_ud;
    eebus_mutex_t*   mutex;
};

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

spine_err_t spine_router_create(spine_router_t** out,
                                  spine_device_t*  local_dev,
                                  spine_send_fn_t  send_fn,
                                  void*            send_ud) {
    if (!out || !local_dev || !send_fn) return SPINE_ERR_INVALID;

    spine_router_t* r = EEBUS_NEW(spine_router_t);
    if (!r) return SPINE_ERR_NO_MEMORY;

    r->local_dev    = local_dev;
    r->remote_count = 0;
    r->send_fn      = send_fn;
    r->send_ud      = send_ud;
    eebus_mutex_create(&r->mutex);

    spine_device_set_router(local_dev, r);

    *out = r;
    return SPINE_OK;
}

void spine_router_destroy(spine_router_t* router) {
    if (!router) return;

    eebus_mutex_lock(router->mutex);
    for (uint8_t i = 0; i < router->remote_count; i++) {
        if (router->remote_devs[i]) {
            spine_device_destroy(router->remote_devs[i]);
            router->remote_devs[i] = NULL;
        }
    }
    router->remote_count = 0;
    eebus_mutex_unlock(router->mutex);

    eebus_mutex_destroy(router->mutex);
    EEBUS_FREE(router);
}

/* ── Query ────────────────────────────────────────────────────────────────── */

spine_device_t* spine_router_find_remote_by_session(spine_router_t* router,
                                                     const char*     session_id) {
    if (!router || !session_id) return NULL;
    for (uint8_t i = 0; i < router->remote_count; i++) {
        spine_device_t* d = router->remote_devs[i];
        if (d && strcmp(d->session_id, session_id) == 0)
            return d;
    }
    return NULL;
}

spine_device_t* spine_router_find_remote_by_ski(spine_router_t* router,
                                                  const char*     peer_ski) {
    if (!router || !peer_ski) return NULL;
    for (uint8_t i = 0; i < router->remote_count; i++) {
        spine_device_t* d = router->remote_devs[i];
        if (d && strcmp(d->ski, peer_ski) == 0)
            return d;
    }
    return NULL;
}

spine_device_t* spine_router_find_remote_by_id(spine_router_t* router,
                                                 const char*     device_id) {
    if (!router || !device_id || !device_id[0]) return NULL;
    for (uint8_t i = 0; i < router->remote_count; i++) {
        spine_device_t* d = router->remote_devs[i];
        if (d && strcmp(d->device_id, device_id) == 0)
            return d;
    }
    return NULL;
}

spine_device_t* spine_router_get_local(spine_router_t* router) {
    return router ? router->local_dev : NULL;
}

void spine_router_foreach_remote(spine_router_t*       router,
                                  spine_router_foreach_fn cb,
                                  void*                   ud) {
    if (!router || !cb) return;
    eebus_mutex_lock(router->mutex);
    for (uint8_t i = 0; i < router->remote_count; i++) {
        if (router->remote_devs[i])
            cb(router->remote_devs[i], ud);
    }
    eebus_mutex_unlock(router->mutex);
}

/* ── SHIP connection established ──────────────────────────────────────────── */

spine_err_t spine_router_on_connect(spine_router_t* router,
                                     const char*     peer_ski,
                                     const char*     session_id) {
    if (!router || !peer_ski || !session_id) return SPINE_ERR_INVALID;

    eebus_mutex_lock(router->mutex);

    /* Prevent duplicates: remotes are keyed by session_id (connection handle).
     * Multiple connections to the same ski are allowed (each gets its own
     * remote); duplicates within the same session_id are not. */
    for (uint8_t i = 0; i < router->remote_count; i++) {
        if (router->remote_devs[i] &&
            strcmp(router->remote_devs[i]->session_id, session_id) == 0) {
            eebus_mutex_unlock(router->mutex);
            EEBUS_LOG_WARN(LOG_TAG,
                           "%s on_connect: duplicate session_id (ski=%s)",
                           session_id, peer_ski);
            return SPINE_OK;
        }
    }

    if (router->remote_count >= SPINE_ROUTER_PEER_MAX) {
        eebus_mutex_unlock(router->mutex);
        return SPINE_ERR_OVERFLOW;
    }

    eebus_mutex_unlock(router->mutex);

    /* create_remote: session_id, local, and router are all passed in */
    spine_device_t* remote = NULL;
    spine_err_t rc = spine_device_create_remote(&remote, peer_ski, router,
                                                 router->local_dev, session_id);
    if (rc != SPINE_OK) return rc;

    eebus_mutex_lock(router->mutex);
    router->remote_devs[router->remote_count++] = remote;
    eebus_mutex_unlock(router->mutex);

    /* Trigger discovery */
    spine_device_start_discovery(remote);

    EEBUS_LOG_INFO(LOG_TAG, "%s connected: ski=%s", session_id, peer_ski);
    return SPINE_OK;
}

/* ── SHIP connection disconnected ─────────────────────────────────────────── */

void spine_router_on_disconnect(spine_router_t* router, const char* session_id) {
    if (!router || !session_id) return;

    eebus_mutex_lock(router->mutex);

    spine_device_t* remote = NULL;
    uint8_t idx = 0;
    for (uint8_t i = 0; i < router->remote_count; i++) {
        if (router->remote_devs[i] &&
            strcmp(router->remote_devs[i]->session_id, session_id) == 0) {
            remote = router->remote_devs[i];
            idx    = i;
            break;
        }
    }

    if (!remote) { eebus_mutex_unlock(router->mutex); return; }

    uint8_t tail = router->remote_count - idx - 1;
    if (tail > 0)
        memmove(&router->remote_devs[idx], &router->remote_devs[idx + 1],
                tail * sizeof(spine_device_t*));
    router->remote_count--;

    eebus_mutex_unlock(router->mutex);

    EEBUS_LOG_INFO(LOG_TAG, "%s disconnected: ski=%s",
                   remote->session_id, remote->ski);
    /* remote's sub_bind is destroyed inside spine_device_destroy() */
    spine_device_destroy(remote);
}

/* ── Message routing (simplified: find remote directly, hand off to device_dispatch) */

spine_err_t spine_router_dispatch(spine_router_t* router,
                                   const char*     session_id,
                                   const char*     json) {
    if (!router || !session_id || !json) return SPINE_ERR_INVALID;

    spine_device_t* remote = spine_router_find_remote_by_session(router, session_id);
    if (!remote) {
        EEBUS_LOG_WARN(LOG_TAG, "dispatch: no remote for session=%s", session_id);
        return SPINE_ERR_NOT_FOUND;
    }
    return spine_device_dispatch(remote, json);
}

/* ── Internal send (routes via dev->session_id) ───────────────────────────── */

void spine_router_send(spine_router_t* router,
                        spine_device_t* dev,
                        const char*     json) {
    if (!router || !dev || !json) return;
    if (!dev->session_id[0]) {
        EEBUS_LOG_WARN(LOG_TAG,
                       "router_send: empty session_id (ski=%s)",
                       dev->ski[0] ? dev->ski : "(empty)");
        return;
    }
    router->send_fn(dev->session_id, json, router->send_ud);
}
