// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/ship_helper.h
 *
 * Convenience wrapper around ship_node_t for EEBus demo programs.
 *
 * Combines TLS credential loading, trust management, mDNS registration,
 * and peer discovery into a single easy-to-configure object.
 */

#ifndef EEBUS_DEMO_SHIP_HELPER_H_
#define EEBUS_DEMO_SHIP_HELPER_H_

#include <stdbool.h>
#include <stdint.h>

#include "eebus_error.h"
#include "ship_node.h"
#include "trust.h"
#include "tls_persist.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ship_helper ship_helper_t;

/**
 * @brief Called when a SPINE JSON payload arrives from a connected peer.
 *
 * @param session_id Connection handle (stable per connection).
 * @param peer_ski   SKI of the sending peer (for logging/identification).
 * @param json       NUL-terminated SPINE JSON string.
 * @param userdata   Value from ship_helper_config_t::userdata.
 */
typedef void (*ship_helper_data_fn)(
    const char* session_id,
    const char* peer_ski,
    const char* json,
    void*       userdata);

/**
 * @brief Called when a peer connects or disconnects.
 *
 * @param peer_ski   SKI of the peer (authenticated identity).
 * @param connected  true = connected, false = disconnected.
 * @param session_id Connection handle "[peerSKI16#xxxxxxxx]" (unique per
 *                   connection instance; use with ship_helper_send).
 * @param userdata   Value from ship_helper_config_t::userdata.
 */
typedef void (*ship_helper_conn_fn)(
    const char* peer_ski,
    bool        connected,
    const char* session_id,
    void*       userdata);

/** Configuration for ship_helper_create(). */
typedef struct {
  /** Demo program name — used for cert persistence and mDNS naming. */
  const char* name;

  /** WebSocket listen port. */
  uint16_t port;

  /** mDNS register flag: true = accept new peers (pairing mode). */
  bool register_flag;

  /** Initial trust mode (TRUST_MODE_MANUAL or TRUST_MODE_AUTO). */
  trust_mode_t trust_mode;

  /**
   * @brief Optional prompt callback for manual trust mode.
   *
   * Called when a new peer is discovered and trust_mode == MANUAL.
   * If NULL, a default console prompt is printed instead.
   */
  trust_prompt_fn trust_prompt;

  /** SPINE data handler — called for every inbound payload. */
  ship_helper_data_fn on_data;

  /** Connection state change handler (optional). */
  ship_helper_conn_fn on_conn;

  /** mDNS peer discovered handler (optional). */
  void (*on_peer_discovered)(const ship_peer_info_t* peer, void* userdata);

  /** mDNS peer lost handler (optional). */
  void (*on_peer_lost)(const ship_peer_info_t* peer, void* userdata);

  /** Forwarded to all callbacks. */
  void* userdata;

  /** Device brand / manufacturer name.  Forwarded to mDNS TXT "brand". */
  const char* brand;

  /** Device type string, e.g. "HeatPump".  Forwarded to mDNS TXT "type". */
  const char* type;

  /** Device model name.  Forwarded to mDNS TXT "model". */
  const char* model;

  /**
   * @brief SHIP device instance identifier (§7.3.2 "id" TXT field).
   *
   * Forwarded to mDNS TXT "id" and to accessMethods.id (§13.4.6 Table 24).
   * Should be a stable, unique string (e.g. serial number).
   * If NULL or empty, instance_name ("EEBus-<name>") is used as fallback.
   */
  const char* ship_id;
} ship_helper_config_t;

/**
 * @brief Create a SHIP helper.
 *
 * Loads (or generates) TLS credentials, creates ship_node_t, and sets up
 * trust management.  Does NOT start the node — call ship_helper_start().
 *
 * @param[out] out  Receives the helper handle.
 * @param[in]  cfg  Configuration.
 * @return EEBUS_OK or EEBUS_ERR_*.
 */
eebus_err_t ship_helper_create(ship_helper_t** out, const ship_helper_config_t* cfg);

/**
 * @brief Start the SHIP node (begin listening and mDNS discovery).
 *
 * Loads previously trusted SKIs from disk, then calls ship_node_start().
 *
 * @param h  Helper handle.
 * @return EEBUS_OK or EEBUS_ERR_*.
 */
eebus_err_t ship_helper_start(ship_helper_t* h);

/**
 * @brief Stop the SHIP node (stop listening and disconnect peers).
 */
void ship_helper_stop(ship_helper_t* h);

/**
 * @brief Stop and destroy the helper, freeing all resources.
 */
void ship_helper_destroy(ship_helper_t* h);

/**
 * @brief Manually connect to a peer by IP and port.
 *
 * The peer's SKI must already be trusted, or trust_mode must be AUTO.
 *
 * @param h     Helper handle.
 * @param ip    IPv4 or IPv6 address string.
 * @param port  Port number.
 * @return EEBUS_OK or EEBUS_ERR_*.
 */
eebus_err_t ship_helper_connect(ship_helper_t* h, const char* ip, uint16_t port);

/**
 * @brief Send a SPINE JSON payload on a specific SHIP connection.
 *
 * @param h           Helper handle.
 * @param session_id  Connection handle (from on_conn / on_data).
 * @param json        NUL-terminated SPINE JSON.
 * @return EEBUS_OK or EEBUS_ERR_*.
 */
eebus_err_t ship_helper_send(ship_helper_t* h, const char* session_id, const char* json);

/**
 * @brief Return the underlying ship_node_t (for direct SDK calls).
 */
ship_node_t* ship_helper_get_node(ship_helper_t* h);

/**
 * @brief Return the trust manager (for CLI trust commands).
 */
trust_mgr_t* ship_helper_get_trust(ship_helper_t* h);

/**
 * @brief Return the local SKI string (40 hex chars + NUL).
 */
const char* ship_helper_get_ski(const ship_helper_t* h);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_DEMO_SHIP_HELPER_H_ */
