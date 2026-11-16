// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  ship_mdns.h
 * @brief SHIP mDNS service advertisement and discovery via Avahi.
 *
 * SHIP uses mDNS-SD to announce and discover nodes on the local network.
 * SHIP §7 specifies the following conventions:
 *
 * - Service type   : "_ship._tcp"
 * - TXT record     : "txtvers=1" "path=/ship/"
 *                    "id=<instance-id>" "ski=<40-hex-ski>"
 *                    "register=<true|false>"
 * - Port           : The WebSocket port of the SHIP node (typically 7711)
 *
 * ## Usage
 * @code
 *   ship_mdns_config_t cfg = {
 *     .instance_name = "MyHeatPump-ABC123",
 *     .port          = 7711,
 *     .ski           = "a1b2c3d4....",   // 40-char SKI of our certificate
 *     .register_flag = true,
 *   };
 *   ship_mdns_t* mdns = NULL;
 *   ship_mdns_create(&mdns, &cfg, on_peer_found, on_peer_removed, userdata);
 *   ship_mdns_start(mdns);
 *   // ... later:
 *   ship_mdns_stop(mdns);
 *   ship_mdns_destroy(mdns);
 * @endcode
 */

#ifndef EEBUS_SRC_SHIP_MDNS_SHIP_MDNS_H_
#define EEBUS_SRC_SHIP_MDNS_SHIP_MDNS_H_

#include <stdbool.h>
#include <stdint.h>

#include "../../common/eebus_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum length for mDNS instance names (UTF-8, Avahi limit ~63 bytes). */
#define SHIP_MDNS_INSTANCE_NAME_MAX (64U)

/** Well-known SHIP service type. SHIP §7.3 */
#define SHIP_MDNS_SERVICE_TYPE "_ship._tcp"

/** Well-known SHIP WebSocket path. SHIP §7.3 */
#define SHIP_MDNS_PATH "/ship/"

/** Default SHIP WebSocket port. */
#define SHIP_MDNS_DEFAULT_PORT (7711U)

/**
 * @brief Information about a discovered SHIP peer.
 *
 * Passed to the peer-found callback.  All string fields are valid only for
 * the duration of the callback; copy them if longer retention is needed.
 */
typedef struct {
  const char* instance_name; /**< mDNS instance name of the peer                */
  const char* host;          /**< TCP connect target: resolved IP or hostname    */
  const char* hostname;      /**< mDNS hostname for TLS SNI (§9.4); may equal
                              *   host when Avahi provides name, not IP          */
  uint16_t    port;          /**< WebSocket port                                 */
  const char* ski;           /**< 40-char SKI hex string (Mandatory §7.3.2)     */
  const char* id;            /**< Device instance ID TXT field (Mandatory §7.3.2) */
  bool        register_flag; /**< "register" TXT field value                     */
  const char* path;          /**< WebSocket path (typically "/ship/")            */
  uint8_t     txtvers;       /**< TXT "txtvers" value (currently always 1)       */
  const char* brand;         /**< TXT "brand" — manufacturer name (optional)    */
  const char* model;         /**< TXT "model" — device model name (optional)    */
  const char* type;          /**< TXT "type"  — device type, e.g. "HeatPump"    */
} ship_mdns_peer_t;

/**
 * @brief Callback invoked when a new SHIP peer is discovered on the network.
 *
 * @param peer     Information about the discovered peer.
 * @param userdata Opaque pointer supplied to ship_mdns_create().
 */
typedef void (*ship_mdns_peer_found_cb)(const ship_mdns_peer_t* peer, void* userdata);

/**
 * @brief Callback invoked when a previously discovered peer disappears.
 *
 * @param instance_name  mDNS instance name of the removed peer.
 * @param userdata       Opaque pointer supplied to ship_mdns_create().
 */
typedef void (*ship_mdns_peer_removed_cb)(const char* instance_name, void* userdata);

/**
 * @brief Configuration for a SHIP mDNS instance.
 */
typedef struct {
  /**
   * @brief mDNS service instance name.
   *
   * Should be unique on the local network, e.g. "<model>-<serial>".
   * Maximum SHIP_MDNS_INSTANCE_NAME_MAX bytes (UTF-8).
   */
  char instance_name[SHIP_MDNS_INSTANCE_NAME_MAX];

  /** WebSocket port to advertise. Typically SHIP_MDNS_DEFAULT_PORT. */
  uint16_t port;

  /** 40-char hex SKI of our TLS certificate. SHIP §7.3.2 */
  char ski[41]; /* 40 chars + NUL */

  /**
   * @brief "register" TXT flag. SHIP §7.3.2
   *
   * true  = this node is willing to accept new connections.
   * false = only connections from already-trusted peers are accepted.
   */
  bool register_flag;

  /**
   * @brief Device instance identifier. SHIP §7.3.2 "id" TXT field.
   *
   * Unique string identifying this device instance, e.g. a serial number.
   * If empty, instance_name is used.
   */
  char id[SHIP_MDNS_INSTANCE_NAME_MAX];

  /** Device brand / manufacturer name. SHIP §7.3.2 "brand" TXT field. */
  char brand[64];

  /** Device type string, e.g. "HeatPump". SHIP §7.3.2 "type" TXT field. */
  char type[64];

  /** Device model name. SHIP §7.3.2 "model" TXT field. */
  char model[64];
} ship_mdns_config_t;

/** @brief Opaque mDNS handle. */
typedef struct ship_mdns ship_mdns_t;

/**
 * @brief Create a new mDNS instance.
 *
 * Does not start advertising or browsing; call ship_mdns_start() for that.
 *
 * @param[out] mdns          Receives the handle.
 * @param[in]  config        Configuration.
 * @param[in]  on_found      Called when a new peer is discovered. May be NULL.
 * @param[in]  on_removed    Called when a peer disappears. May be NULL.
 * @param[in]  userdata      Forwarded to callbacks.
 * @return EEBUS_OK or EEBUS_ERR_*.
 */
eebus_err_t ship_mdns_create(
    ship_mdns_t**            mdns,
    const ship_mdns_config_t* config,
    ship_mdns_peer_found_cb  on_found,
    ship_mdns_peer_removed_cb on_removed,
    void*                    userdata
);

/**
 * @brief Start advertising this node and browsing for peers.
 *
 * Spawns an internal thread to drive the Avahi event loop.
 *
 * @param[in] mdns  Handle from ship_mdns_create().
 * @return EEBUS_OK or EEBUS_ERR_INTERNAL.
 */
eebus_err_t ship_mdns_start(ship_mdns_t* mdns);

/**
 * @brief Stop advertising and browsing.
 *
 * Blocks until the internal event-loop thread has exited.
 *
 * @param[in] mdns  Handle from ship_mdns_create().
 */
void ship_mdns_stop(ship_mdns_t* mdns);

/**
 * @brief Destroy the handle and release all resources.
 *
 * ship_mdns_stop() must be called first.  Passing NULL is a no-op.
 *
 * @param[in] mdns  Handle to destroy.
 */
void ship_mdns_destroy(ship_mdns_t* mdns);

/**
 * @brief Update the "register" flag in the published TXT record at runtime.
 *
 * Changes take effect immediately in the next mDNS announcement cycle.
 *
 * @param[in] mdns            Handle from ship_mdns_create().
 * @param[in] register_flag   New value.
 * @return EEBUS_OK or EEBUS_ERR_STATE if not started.
 */
eebus_err_t ship_mdns_set_register(ship_mdns_t* mdns, bool register_flag);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_SRC_SHIP_MDNS_SHIP_MDNS_H_ */
