// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_node_mgmt.h
 *
 * NodeManagement feature handler (internal module, only included by spine_device.c).
 *
 * nm is the handler for the NodeManagement feature. Responsibilities:
 *   READ  -> Build discovery reply from dev->features[] / dev->uc_entries[]
 *   REPLY -> Parse peer discovery data, write to dev->features[] / dev->uc_entries[],
 *            advance dev->disc_state, drive next step (UC discovery -> NM subscribe)
 *   RESULT-> Match dev->pending[], advance to READY, trigger dev->peer_ready_cbs[]
 *   CALL  -> Parse sub/bind requests, write to dev->sub_bind, reply with RESULT
 *   NOTIFY-> Parse feature changes, update dev->features[] / dev->sub_bind
 *
 * All state data (sub_bind, uc_entries, features, disc_state, pending) resides on dev;
 * nm accesses them directly via the dev pointer.
 */

#ifndef SPINE_NODE_MGMT_H_
#define SPINE_NODE_MGMT_H_

#include "spine_device_internal.h"
#include "codec/spine_codec_node_mgmt.h"
#include "codec/spine_codec_sub_bind.h"
#include "codec/spine_codec_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spine_node_mgmt spine_node_mgmt_t;

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/**
 * @brief Create nm and register the NodeManagement feature handler on dev.
 * nm only holds a dev pointer; all data is accessed through dev.
 */
spine_err_t spine_node_mgmt_create(spine_node_mgmt_t** out,
                                    spine_device_t*     dev);

void spine_node_mgmt_destroy(spine_node_mgmt_t* nm);

/* ── Active initiation interface ──────────────────────────────────────────── */

/**
 * @brief Initiate DetailedDiscovery READ (called after remote device creation).
 * Allocates a pending counter, advances dev->disc_state = DISCOVERING, sends the message.
 */
spine_err_t spine_node_mgmt_start_discovery(spine_node_mgmt_t* nm);

/**
 * @brief Send a subscribe CALL to the peer NM feature.
 * peer_nm is derived from server_addr.device + entity[0]/feature[0].
 */
spine_err_t spine_node_mgmt_send_subscribe(spine_node_mgmt_t*  nm,
                                            const spine_addr_t* client_addr,
                                            const spine_addr_t* server_addr);

/**
 * @brief Send a bind CALL to the peer NM feature.
 */
spine_err_t spine_node_mgmt_send_bind(spine_node_mgmt_t*  nm,
                                       const spine_addr_t* client_addr,
                                       const spine_addr_t* server_addr);

/**
 * @brief Update use case availability and notify all NM subscribers (§7.2).
 *
 * Finds the use case by name in local->uc_entries[], updates available,
 * and sends nodeManagementUseCaseData NOTIFY if the value changed.
 */
/**
 * @brief Send a subscription delete CALL to the peer NM (§7.4.3).
 */
spine_err_t spine_node_mgmt_send_unsubscribe(spine_node_mgmt_t*  nm,
                                              const spine_addr_t* client_addr,
                                              const spine_addr_t* server_addr);

/**
 * @brief Send a binding delete CALL to the peer NM (§7.3.3).
 */
spine_err_t spine_node_mgmt_send_unbind(spine_node_mgmt_t*  nm,
                                         const spine_addr_t* client_addr,
                                         const spine_addr_t* server_addr);

spine_err_t spine_node_mgmt_set_usecase_available(spine_node_mgmt_t* nm,
                                                    const char* use_case_name,
                                                    bool available);

/**
 * @brief Send nodeManagementDetailedDiscoveryData NOTIFY to all NM subscribers.
 * Call after dynamically adding/removing entities or features (§7.1.5).
 */
spine_err_t spine_node_mgmt_notify_discovery(spine_node_mgmt_t* nm);

/**
 * @brief Send nodeManagementUseCaseData NOTIFY to all NM subscribers.
 * Call after batch-modifying use case entries (§7.2).
 */
spine_err_t spine_node_mgmt_notify_usecases(spine_node_mgmt_t* nm);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_NODE_MGMT_H_ */
