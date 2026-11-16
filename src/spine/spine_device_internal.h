// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_device_internal.h
 *
 * Full structure definition of spine_device_t.
 * Only for spine internal modules (spine_device.c, spine_node_mgmt.c, spine_router.c) to include.
 * Application and UC layers can only access via the opaque pointer in spine_device.h.
 */

#ifndef SPINE_DEVICE_INTERNAL_H_
#define SPINE_DEVICE_INTERNAL_H_

#include "spine_device.h"
#include "spine_sub_bind.h"

#include "../platform/eebus_mutex.h"

/* ── Internal forward declarations ────────────────────────────────────────── */

typedef struct spine_node_mgmt spine_node_mgmt_t;

/* ── Internal constants ───────────────────────────────────────────────────── */

#define SPINE_FEATURE_MAX     64
#define SPINE_PEER_READY_MAX   16
#define SPINE_PENDING_MAX     32

/* ── pending requests (for matching async reply/result) ───────────────────── */

typedef enum {
    SPINE_PENDING_NONE         = 0,
    SPINE_PENDING_DISC_DETAIL,      /* waiting for DetailedDiscovery reply */
    SPINE_PENDING_DISC_UC,          /* waiting for UseCaseData reply */
    SPINE_PENDING_NM_SUBSCRIBE,     /* waiting for NM subscribe result (within discovery flow) */
    SPINE_PENDING_SUBSCRIBE,        /* waiting for UC layer subscribe result */
    SPINE_PENDING_BIND,             /* waiting for UC layer bind result */
} spine_pending_type_t;

typedef struct {
    bool                 in_use;
    uint64_t             counter;
    spine_pending_type_t type;
    spine_addr_t         client_addr;   /* valid for SUBSCRIBE/BIND */
    spine_addr_t         server_addr;   /* valid for SUBSCRIBE/BIND */
    uint64_t             created_ms;    /* CLOCK_MONOTONIC ms, for timeout detection */
} spine_pending_entry_t;

/* ── discovery state machine (remote only) ────────────────────────────────── */

typedef enum {
    SPINE_DISC_CREATED       = 0,
    SPINE_DISC_DISCOVERING,         /* 1st READ DetailedDiscovery */
    SPINE_DISC_NM_SUBSCRIBING,      /* CALL Subscribe (after 1st discovery) */
    SPINE_DISC_REDISCOVERING,       /* 2nd READ DetailedDiscovery (after subscribe) */
    SPINE_DISC_UC_DISCOVERING,      /* READ UseCaseData */
    SPINE_DISC_READY,
} spine_disc_state_t;

/* ── feature slot ─────────────────────────────────────────────────────────── */

typedef struct {
    spine_feature_info_t    info;
    spine_feature_handler_t handler;
    void*                   userdata;
} feature_slot_t;

/* ── peer_ready / peer_changed callback slots ────────────────────────────── */

typedef struct {
    spine_peer_ready_fn fn;
    void*               ud;
} peer_ready_slot_t;

typedef struct {
    spine_peer_changed_fn fn;
    void*                 ud;
} peer_changed_slot_t;

/* ── spine_device_t full structure ────────────────────────────────────────── */

struct spine_device {
    /* ── Basic identification ───────────────────────────────────────────── */
    char               device_id[SPINE_DEVICE_ID_MAX];
    char               ski[SKI_STR_MAX];
    char               device_type[SPINE_ENTITY_TYPE_MAX]; /* set by local; read by nm */
    bool               is_remote;
    char               session_id[64];
    uint64_t           msg_counter;
    eebus_mutex_t*     mutex;

    /* ── local instance reference (remote points to local; local itself is NULL) */
    spine_device_t*    local;

    /* ── router back-reference ──────────────────────────────────────────── */
    spine_router_t*    router;

    /* ── Entity registry (local: registered by UC; remote: filled by discovery) */
    spine_entity_info_t entities[SPINE_ENTITY_MAX];
    uint8_t             entity_count;

    /* ── Features registry
     * local: registered by UC layer via spine_device_add_feature (incl. nm handler)
     * remote: filled by discovery reply (handler/userdata are NULL)
     * dispatch always looks up get_local(dev)->features[]
     */
    feature_slot_t     features[SPINE_FEATURE_MAX];
    uint8_t            feature_count;

    /* ── usecase metadata
     * local: registered by application layer via spine_device_add_usecase
     * remote: filled by nm after UC discovery reply
     */
    spine_nm_uc_entry_t uc_entries[SPINE_NM_UC_MAX];
    uint8_t             uc_entry_count;

    /* ── subscription/binding container
     * local: holds NM-level subscriptions (optional, may be unused)
     * remote: holds all subscriptions/bindings for this specific peer connection
     *         automatically destroyed with the remote device on disconnect */
    spine_sub_bind_t*  sub_bind;

    /* ── peer_ready callbacks (registered on local only; remote accesses via get_local(dev)) */
    peer_ready_slot_t  peer_ready_cbs[SPINE_PEER_READY_MAX];
    uint8_t            peer_ready_count;

    /* ── peer_changed callbacks (fired when peer capabilities change via NOTIFY) */
    peer_changed_slot_t peer_changed_cbs[SPINE_PEER_READY_MAX];
    uint8_t             peer_changed_count;

    /* ── pending requests (remote only, for matching async reply/result) ── */
    spine_pending_entry_t pending[SPINE_PENDING_MAX];

    /* ── remote only: discovery state machine ───────────────────────── */
    spine_disc_state_t disc_state;

    /* ── nm internal submodule ──────────────────────────────────────── */
    spine_node_mgmt_t* nm;

    /* ── per-device cache slots (UC-level data, e.g. lc_eg, mpc_ma) ── */
    struct {
        void* data;
        void (*destroy)(void*);
    } cache[SPINE_CACHE_SLOT_MAX];
};

/* ── get_local: unified helper function
 * local device: returns itself (local->local == NULL)
 * remote device: returns dev->local
 * All places that need local data should uniformly use this function
 */
static inline spine_device_t* get_local(spine_device_t* dev) {
    return (dev && dev->local) ? dev->local : dev;
}

/* ── Internal utility API ─────────────────────────────────────────────────── */

/** Add pending (DISC type, no address) */
uint64_t spine_device_pending_add(spine_device_t*      dev,
                                   spine_pending_type_t type);

/** Add pending (SUBSCRIBE/BIND type, with client/server addresses) */
uint64_t spine_device_pending_add_sb(spine_device_t*      dev,
                                      spine_pending_type_t type,
                                      const spine_addr_t*  client,
                                      const spine_addr_t*  server);

/** Find and remove pending by counter; client_out/server_out may be NULL */
spine_pending_type_t spine_device_pending_consume(spine_device_t* dev,
                                                   uint64_t        counter,
                                                   spine_addr_t*   client_out,
                                                   spine_addr_t*   server_out);

/** Set router back-reference (called within spine_router_create) */
void spine_device_set_router(spine_device_t* dev, spine_router_t* router);

/** Check if any other remote device has a binding to the given server address.
 *  Used by NM for binding exclusivity enforcement. */
bool spine_device_is_server_bound_by_other(spine_device_t*     dev,
                                            const spine_addr_t* server_addr);

#endif /* SPINE_DEVICE_INTERNAL_H_ */
