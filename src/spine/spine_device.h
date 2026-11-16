// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_device.h
 *
 * SPINE device public API.
 *
 * Two instance semantics (same spine_device_t implementation):
 *
 * local device (spine_device_create_local):
 *   Storage for local capabilities and metadata.
 *   Holds: features (with handlers), usecases, device_type,
 *          sub_bind, peer_ready_cbs, device_id, ski.
 *   Created by the application layer; UC layer registers capabilities;
 *   spine_router holds a reference.
 *
 * remote device (spine_device_create_remote):
 *   Virtual mirror of a remote device, responsible for all message
 *   interactions with that peer.
 *   Holds: peer ski, peer device_id (filled after discovery),
 *          peer uc_entries (filled after UC discovery), disc_state, pending[].
 *   All outbound messages are routed to send_fn via dev->ski.
 *   All inbound messages during dispatch look up get_local(dev)->features[]
 *   to find the handler, passing dev (remote) itself as the device parameter
 *   to the handler.
 *   Local data is accessed via dev->local->xxx when needed.
 *
 * Application and UC layers should only include this header,
 * not spine_device_internal.h.
 */

#ifndef SPINE_DEVICE_H_
#define SPINE_DEVICE_H_

#include <stdint.h>
#include <stdbool.h>
#include "cjson/cJSON.h"
#include "codec/spine_codec_address.h"
#include "codec/spine_codec_msg.h"
#include "codec/spine_codec_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Forward declarations ─────────────────────────────────────────────────── */

typedef struct spine_device spine_device_t;
typedef struct spine_router spine_router_t;

/* ── Constants ────────────────────────────────────────────────────────────── */

#define SKI_STR_MAX   48

/* ── feature handler ──────────────────────────────────────────────────────── */

/**
 * dev: the caller device passed in by dispatch (local or remote).
 * When dispatched through a remote device, dev is remote;
 * handler accesses local info via dev->local->xxx (assisted by get_local).
 */
typedef void (*spine_feature_handler_t)(spine_device_t*    dev,
                                         const spine_msg_t* msg,
                                         void*              userdata);

/* ── Entity info (for discovery reply) ────────────────────────────────────── */

#define SPINE_ENTITY_MAX         32
#define SPINE_ENTITY_DEPTH_MAX    8

typedef struct {
    uint8_t entity[SPINE_ENTITY_DEPTH_MAX];
    uint8_t entity_depth;
    char    entity_type[SPINE_ENTITY_TYPE_MAX];
} spine_entity_info_t;

/* ── Supported function entry (per feature) ──────────────────────────────── */

#define SPINE_SUPPORTED_FN_MAX    8

typedef struct {
    char function_name[SPINE_FUNCTION_NAME_MAX];
    bool read_present;
    bool read_partial;
    bool write_present;
    bool write_partial;
} spine_supported_fn_t;

/* ── feature info ─────────────────────────────────────────────────────────── */

typedef struct {
    spine_addr_t         addr;
    char                 feature_type[SPINE_FEATURE_TYPE_MAX];
    char                 role[SPINE_ROLE_MAX];
    spine_supported_fn_t supported_fns[SPINE_SUPPORTED_FN_MAX];
    uint8_t              supported_fn_count;
} spine_feature_info_t;

/* ── Use Case metadata ────────────────────────────────────────────────────── */

#define SPINE_NM_UC_MAX       16
#define SPINE_UC_SCENARIO_MAX 8

typedef struct {
    char         actor[SPINE_ACTOR_MAX];
    char         use_case_name[SPINE_UC_NAME_MAX];
    char         use_case_version[SPINE_UC_VERSION_MAX];
    char         use_case_doc_sub_revision[32];
    uint8_t      scenario_support[SPINE_UC_SCENARIO_MAX];
    uint8_t      scenario_count;
    spine_addr_t address;
    bool         available;       /* runtime use case availability (SPINE §7.2) */
} spine_nm_uc_entry_t;

/* ── peer_ready callback ──────────────────────────────────────────────────── */

typedef void (*spine_peer_ready_fn)(spine_device_t*            remote_dev,
                                     const spine_nm_uc_entry_t* uc_infos,
                                     uint8_t                    uc_count,
                                     void*                      ud);

/* ── peer_changed callback (fired when peer capabilities change via NOTIFY) ── */

typedef void (*spine_peer_changed_fn)(spine_device_t* remote_dev,
                                       void*           ud);

/* ════════════════════════════════════════════════════════════════════════════
 * Lifecycle
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Create a local device (local capability storage).
 *
 * @param device_id  Local SPINE device address ("d:_i:xxx" format)
 * @param ski        Local SKI
 * @param device_type Device type (e.g. "EnergyManagementSystem")
 */
spine_err_t spine_device_create_local(spine_device_t** out,
                                       const char*      device_id,
                                       const char*      ski,
                                       const char*      device_type);

/**
 * @brief Create a remote device (virtual mirror of the peer).
 * Called internally by spine_router_on_connect; not used directly
 * by the application layer.
 *
 * @param ski        Peer SKI (known at connection establishment)
 * @param router     Owning router
 * @param local      Pointer to the local device
 * @param session_id Log correlation string (may be NULL)
 */
spine_err_t spine_device_create_remote(spine_device_t** out,
                                        const char*      ski,
                                        spine_router_t*  router,
                                        spine_device_t*  local,
                                        const char*      session_id);

void spine_device_destroy(spine_device_t* dev);

/* ════════════════════════════════════════════════════════════════════════════
 * Entity / Feature / UseCase / peer_ready registration (local device)
 *
 * NOT thread-safe. Must be called during initialization from
 * a single thread.
 * ════════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Register an entity on the local device for discovery replies.
 *
 * Entity [0] "DeviceInformation" is auto-registered at creation time.
 * UC layers should register their entities (e.g., entity [1] "CEM").
 */
spine_err_t spine_device_add_entity(spine_device_t*  dev,
                                     const uint8_t*   entity_path,
                                     uint8_t          entity_depth,
                                     const char*      entity_type);

spine_err_t spine_device_add_feature(spine_device_t*         dev,
                                      const spine_addr_t*     addr,
                                      const char*             feature_type,
                                      const char*             role,
                                      spine_feature_handler_t handler,
                                      void*                   userdata);

/**
 * @brief Register a supported function on an already-registered feature.
 *
 * Call after spine_device_add_feature(). The function_name must match the
 * spec-defined name exactly (e.g., "loadControlLimitListData").
 */
spine_err_t spine_device_add_feature_function(spine_device_t*  dev,
                                               const spine_addr_t* feature_addr,
                                               const char*      function_name,
                                               bool read_present,
                                               bool read_partial,
                                               bool write_present,
                                               bool write_partial);

const spine_feature_info_t* spine_device_find_feature(const spine_device_t* dev,
                                                        const spine_addr_t*   addr);

const spine_feature_info_t* spine_device_get_feature(const spine_device_t* dev,
                                                       uint8_t               idx);

uint8_t spine_device_feature_count(const spine_device_t* dev);

spine_err_t spine_device_add_usecase(spine_device_t*            dev,
                                      const spine_nm_uc_entry_t* entry);

const spine_nm_uc_entry_t* spine_device_get_usecases(const spine_device_t* dev,
                                                       uint8_t*              count_out);

/**
 * @brief Set runtime availability of a registered use case (SPINE §7.2).
 *
 * When the value changes, a nodeManagementUseCaseData NOTIFY is sent
 * to all NM subscribers. Delegates to spine_node_mgmt internally.
 */
spine_err_t spine_device_set_usecase_available(spine_device_t* dev,
                                                const char*     use_case_name,
                                                bool            available);

spine_err_t spine_device_add_peer_ready_cb(spine_device_t*    dev,
                                             spine_peer_ready_fn fn,
                                             void*               ud);

/**
 * @brief Register a callback fired when a connected peer's capabilities
 * change via DetailedDiscovery or UseCaseData NOTIFY.
 * UC/application layer can re-evaluate bindings and subscriptions.
 */
spine_err_t spine_device_add_peer_changed_cb(spine_device_t*      dev,
                                               spine_peer_changed_fn fn,
                                               void*                 ud);

/**
 * @brief Send DetailedDiscovery NOTIFY to all NM subscribers (§7.1.5).
 * Call after dynamically adding/removing entities or features.
 */
spine_err_t spine_device_notify_discovery(spine_device_t* dev);

/**
 * @brief Send UseCaseData NOTIFY to all NM subscribers (§7.2).
 * Call after batch-modifying use case entries.
 */
spine_err_t spine_device_notify_usecases(spine_device_t* dev);

/* ════════════════════════════════════════════════════════════════════════════
 * Discovery (triggers NM state machine on a remote device)
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_start_discovery(spine_device_t* dev);

/* ════════════════════════════════════════════════════════════════════════════
 * Dispatch / Send (with dev as the message routing target)
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_dispatch(spine_device_t* dev, const char* json);

spine_err_t spine_device_send(spine_device_t* dev, const spine_msg_t* msg);

spine_err_t spine_device_send_result(spine_device_t*       dev,
                                      const spine_header_t* req_header,
                                      uint32_t              error_number,
                                      const char*           description);

/* ════════════════════════════════════════════════════════════════════════════
 * Notify (broadcast to all clients subscribed to server_addr)
 *
 * dev: can be any instance (local or remote);
 *      subscription relationships are looked up from each remote's sub_bind;
 *      the corresponding remote device is found via dev->router for sending.
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_notify(spine_device_t*     dev,
                                 const spine_addr_t* server_addr,
                                 const spine_msg_t*  msg);

/* ════════════════════════════════════════════════════════════════════════════
 * Subscribe / Bind (actively initiated towards a remote device)
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_subscribe(spine_device_t*     dev,
                                    const spine_addr_t* client_addr,
                                    const spine_addr_t* server_addr);

spine_err_t spine_device_bind(spine_device_t*     dev,
                               const spine_addr_t* client_addr,
                               const spine_addr_t* server_addr);

/** Send a subscription delete CALL (§7.4.3). Removes local entry and notifies peer. */
spine_err_t spine_device_unsubscribe(spine_device_t*     dev,
                                      const spine_addr_t* client_addr,
                                      const spine_addr_t* server_addr);

/** Send a binding delete CALL (§7.3.3). Removes local entry and notifies peer. */
spine_err_t spine_device_unbind(spine_device_t*     dev,
                                 const spine_addr_t* client_addr,
                                 const spine_addr_t* server_addr);

/* ════════════════════════════════════════════════════════════════════════════
 * Query / Utilities
 * ════════════════════════════════════════════════════════════════════════════ */

const char* spine_device_get_id(const spine_device_t* dev);
const char* spine_device_get_ski(const spine_device_t* dev);
bool        spine_device_is_remote(const spine_device_t* dev);

bool spine_device_has_binding(const spine_device_t* dev,
                               const spine_addr_t*   client,
                               const spine_addr_t*   server);

/** Check if this remote device has any binding targeting the given server. */
bool spine_device_has_binding_to_server(const spine_device_t* dev,
                                         const spine_addr_t*   server);

bool spine_device_has_subscription(const spine_device_t* dev,
                                    const spine_addr_t*   client,
                                    const spine_addr_t*   server);

/** Find entity_type by entity address (e.g. from uc_entry->address). */
const char* spine_device_get_entity_type(const spine_device_t* dev,
                                          const spine_addr_t*   entity_addr);

uint64_t    spine_device_next_msg_counter(spine_device_t* dev);
void        spine_device_set_session_id(spine_device_t* dev, const char* sid);
const char* spine_device_get_session_id(const spine_device_t* dev);
/** Send notify to subscribers on a specific remote device only. */
spine_err_t spine_device_notify_to(spine_device_t*     remote_dev,
                                    const spine_addr_t* server_addr,
                                    const spine_msg_t*  msg);

/** Find a remote device by session_id (via internal router lookup). */
spine_device_t* spine_device_find_remote(const spine_device_t* local_dev,
                                          const char* session_id);

/* ════════════════════════════════════════════════════════════════════════════
 * Per-device cache slots (UC-level data attached to remote devices)
 * ════════════════════════════════════════════════════════════════════════════ */

#define SPINE_CACHE_SLOT_MAX  4
#define SPINE_CACHE_LC_EG     0
#define SPINE_CACHE_MPC_MA    1

/** Get cache data for a slot (NULL if not set). */
void* spine_device_get_cache(const spine_device_t* dev, uint8_t slot);

/** Set cache data for a slot. destroy is called on dev destroy or overwrite. */
void  spine_device_set_cache(spine_device_t* dev, uint8_t slot,
                              void* data, void (*destroy)(void*));

#ifdef __cplusplus
}
#endif

#endif /* SPINE_DEVICE_H_ */
