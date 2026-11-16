// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  ship_mdns_avahi.c
 * @brief Avahi-based mDNS implementation for SHIP.
 *
 * SHIP §7: mDNS-SD is used to advertise and discover SHIP nodes.
 *
 * This implementation:
 *   1. Publishes "_ship._tcp" service with the required TXT records.
 *   2. Browses for "_ship._tcp" peers, resolves their addresses,
 *      and parses their TXT records (ski, register, path).
 *   3. Runs the Avahi simple poll in a dedicated background thread.
 */

#include "ship_mdns.h"

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../log/eebus_log.h"
#include "../../platform/eebus_thread.h"
#include "../../common/eebus_memory.h"

#define LOG_TAG "mdns"

/* ── Internal structure ──────────────────────────────────────────────────── */

struct ship_mdns {
  ship_mdns_config_t       config;
  ship_mdns_peer_found_cb  on_found;
  ship_mdns_peer_removed_cb on_removed;
  void*                    userdata;

  AvahiSimplePoll*  poll;
  AvahiClient*      client;
  AvahiEntryGroup*  group;
  AvahiServiceBrowser* browser;

  eebus_thread_t* thread;
  bool       thread_running;
  bool       started;
};

/* ── TXT record helpers ──────────────────────────────────────────────────── */

/**
 * @brief Build an AvahiStringList with SHIP's required TXT fields.
 *
 * SHIP §7.3.2:
 *   txtvers=1   path=/ship/   id=<instance>   ski=<ski>   register=<true|false>
 */
static AvahiStringList* build_txt(const ship_mdns_t* m) {
  char id_entry[SHIP_MDNS_INSTANCE_NAME_MAX + 4];
  char ski_entry[64];
  char reg_entry[24];
  char brand_entry[72];
  char type_entry[72];
  char model_entry[72];

  /* id= falls back to instance_name when not explicitly set */
  const char* id = (m->config.id[0] != '\0') ? m->config.id
                                              : m->config.instance_name;
  snprintf(id_entry,    sizeof(id_entry),    "id=%s",       id);
  snprintf(ski_entry,   sizeof(ski_entry),   "ski=%s",      m->config.ski);
  snprintf(reg_entry,   sizeof(reg_entry),   "register=%s", m->config.register_flag ? "true" : "false");
  snprintf(brand_entry, sizeof(brand_entry), "brand=%s",    m->config.brand);
  snprintf(type_entry,  sizeof(type_entry),  "type=%s",     m->config.type);
  snprintf(model_entry, sizeof(model_entry), "model=%s",    m->config.model);

  /* avahi_string_list_new prepends each argument, so the resulting list
   * is in REVERSE order of the argument list.  To get wire order:
   *   txtvers=1  id=  path=  ski=  register=  brand=  type=  model=
   * we pass them in the opposite order (model first, txtvers=1 last). */
  return avahi_string_list_new(
      model_entry,
      type_entry,
      brand_entry,
      reg_entry,
      ski_entry,
      "path=" SHIP_MDNS_PATH,
      id_entry,
      "txtvers=1",
      NULL
  );
}

/* ── Entry group (publish) ───────────────────────────────────────────────── */

static void create_services(ship_mdns_t* m);

/* Forward declarations */
static void browse_callback(
    AvahiServiceBrowser*, AvahiIfIndex, AvahiProtocol, AvahiBrowserEvent,
    const char*, const char*, const char*, AvahiLookupResultFlags, void*);

static void group_callback(
    AvahiEntryGroup*      group,
    AvahiEntryGroupState  state,
    void*                 userdata
) {
  (void)group;
  ship_mdns_t* m = (ship_mdns_t*)userdata;

  switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
      EEBUS_LOG_INFO(LOG_TAG, "mDNS service '%s' registered", m->config.instance_name);
      break;

    case AVAHI_ENTRY_GROUP_COLLISION: {
      /* Name collision: rename and try again */
      char* alt = avahi_alternative_service_name(m->config.instance_name);
      EEBUS_LOG_WARN(LOG_TAG, "mDNS name collision, renaming to '%s'", alt);
      snprintf(m->config.instance_name, sizeof(m->config.instance_name), "%s", alt);
      avahi_free(alt);
      create_services(m);
      break;
    }

    case AVAHI_ENTRY_GROUP_FAILURE:
      EEBUS_LOG_ERROR(LOG_TAG, "mDNS entry group failure: %s",
                      avahi_strerror(avahi_client_errno(m->client)));
      avahi_simple_poll_quit(m->poll);
      break;

    default:
      break;
  }
}

static void create_services(ship_mdns_t* m) {
  if (!m->group) {
    m->group = avahi_entry_group_new(m->client, group_callback, m);
    if (!m->group) {
      EEBUS_LOG_ERROR(LOG_TAG, "avahi_entry_group_new failed: %s",
                      avahi_strerror(avahi_client_errno(m->client)));
      return;
    }
  }

  if (avahi_entry_group_is_empty(m->group)) {
    AvahiStringList* txt = build_txt(m);

    /*
     * SHIP §7.3.2 recommends a TTL of 120 seconds for mDNS records.
     * Avahi's high-level API (avahi_entry_group_add_service_strlst) does not
     * expose a TTL parameter; the daemon uses its own default (~4500 s per
     * DNS-SD convention).
     *
     * The alternative – avahi_entry_group_add_record() – accepts a TTL but
     * requires manually registering PTR, SRV and TXT as independent records,
     * which breaks Avahi's automatic "additional records" optimisation
     * (RFC 6763 §11): without the semantic link between the three record
     * types, Avahi no longer bundles SRV+TXT alongside a PTR response,
     * forcing queriers to issue additional round-trips.  The trade-off
     * (correct TTL vs. one-shot responses) is unfavourable for a LAN
     * protocol where devices are typically long-lived.
     *
     * Known deviation: TTL will be Avahi's default rather than 120 s.
     * Operators who need strict §7.3.2 compliance can set a system-wide TTL
     * via /etc/avahi/avahi-daemon.conf (cache-entries-max, or by patching
     * avahi with a custom TTL policy).
     */
    int rc = avahi_entry_group_add_service_strlst(
        m->group,
        AVAHI_IF_UNSPEC,
        AVAHI_PROTO_UNSPEC,
        0,
        m->config.instance_name,
        SHIP_MDNS_SERVICE_TYPE,
        NULL,
        NULL,
        m->config.port,
        txt
    );
    avahi_string_list_free(txt);

    if (rc < 0) {
      EEBUS_LOG_ERROR(LOG_TAG, "avahi_entry_group_add_service failed: %s",
                      avahi_strerror(rc));
      return;
    }

    rc = avahi_entry_group_commit(m->group);
    if (rc < 0) {
      EEBUS_LOG_ERROR(LOG_TAG, "avahi_entry_group_commit failed: %s",
                      avahi_strerror(rc));
    }
  }
}

/* ── Service resolver ────────────────────────────────────────────────────── */

static void resolve_callback(
    AvahiServiceResolver*        resolver,
    AvahiIfIndex                 iface,
    AvahiProtocol                proto,
    AvahiResolverEvent           event,
    const char*                  name,
    const char*                  type,
    const char*                  domain,
    const char*                  host_name,
    const AvahiAddress*          address,
    uint16_t                     port,
    AvahiStringList*             txt,
    AvahiLookupResultFlags       flags,
    void*                        userdata
) {
  (void)iface; (void)proto; (void)type; (void)domain; (void)flags;

  ship_mdns_t* m = (ship_mdns_t*)userdata;

  if (event == AVAHI_RESOLVER_FOUND && m->on_found) {
    /* Parse TXT records.
     *
     * SHIP §7.3.2: "txtvers SHALL be the first key in the TXT record …
     * Clients SHALL silently discard TXT records with txtvers keys that are
     * not present or have a different value than 1."
     *
     * We do a two-pass approach: first scan for txtvers, then (if valid)
     * extract the operational fields.
     */
    char ski[41]    = {0};
    char id[64]     = {0};
    char path[64]   = "/ship/";
    char brand[64]  = {0};
    char model[64]  = {0};
    char type_v[64] = {0};
    uint8_t txtvers_val = 0;
    bool reg_flag   = false;
    bool txtvers_ok = false;

    AvahiStringList* entry = txt;
    while (entry) {
      char*  key   = NULL;
      char*  value = NULL;
      size_t vlen  = 0;

      if (avahi_string_list_get_pair(entry, &key, &value, &vlen) == 0) {
        if (key && strcmp(key, "txtvers") == 0) {
          /* §7.3.2: value must be exactly "1" */
          txtvers_ok = (value && strcmp(value, "1") == 0);
          if (txtvers_ok)
            txtvers_val = 1;
          else {
            EEBUS_LOG_WARN(LOG_TAG,
                           "Ignoring peer '%s': txtvers='%s' (expected '1') "
                           "(SHIP §7.3.2)",
                           name ? name : "?", value ? value : "<absent>");
          }
        } else if (key && strcmp(key, "ski") == 0 && value) {
          snprintf(ski, sizeof(ski), "%s", value);
        } else if (key && strcmp(key, "id") == 0 && value) {
          /* §7.3.2: id is Mandatory – unique device instance identifier */
          snprintf(id, sizeof(id), "%s", value);
        } else if (key && strcmp(key, "path") == 0 && value) {
          snprintf(path, sizeof(path), "%s", value);
        } else if (key && strcmp(key, "register") == 0 && value) {
          reg_flag = (strcmp(value, "true") == 0);
        } else if (key && strcmp(key, "brand") == 0 && value) {
          snprintf(brand, sizeof(brand), "%s", value);
        } else if (key && strcmp(key, "model") == 0 && value) {
          snprintf(model, sizeof(model), "%s", value);
        } else if (key && strcmp(key, "type") == 0 && value) {
          snprintf(type_v, sizeof(type_v), "%s", value);
        }
        /* Unknown keys are silently ignored (§7.3.2) */
        avahi_free(key);
        avahi_free(value);
      }
      entry = avahi_string_list_get_next(entry);
    }

    /* §7.3.2: discard record if txtvers is absent or not "1" */
    if (!txtvers_ok) {
      avahi_service_resolver_free(resolver);
      return;
    }

    char addr_str[AVAHI_ADDRESS_STR_MAX];
    avahi_address_snprint(addr_str, sizeof(addr_str), address);

    /*
     * SHIP §9.4: SNI MUST be supported.
     * Use the mDNS hostname (host_name) as the SNI value so the TLS
     * ClientHello carries a DNS name rather than an IP address.
     * host_name is always a proper DNS name from Avahi; addr_str is the
     * resolved IP used as the TCP connect target.
     */
    ship_mdns_peer_t peer = {
      .instance_name = name,
      .host          = addr_str,
      .hostname      = host_name ? host_name : addr_str,
      .port          = port,
      .ski           = ski,
      .id            = id,
      .register_flag = reg_flag,
      .path          = path,
      .txtvers       = txtvers_val,
      .brand         = brand,
      .model         = model,
      .type          = type_v,
    };

    EEBUS_LOG_INFO(LOG_TAG, "Discovered peer '%s' id=%s host=%s hostname=%s port=%u ski=%s",
                   name, id[0] ? id : "(none)", peer.host, peer.hostname, port, ski);

    m->on_found(&peer, m->userdata);
  }

  avahi_service_resolver_free(resolver);
}

/* ── Service browser ─────────────────────────────────────────────────────── */

static void browse_callback(
    AvahiServiceBrowser*   browser,
    AvahiIfIndex           iface,
    AvahiProtocol          proto,
    AvahiBrowserEvent      event,
    const char*            name,
    const char*            type,
    const char*            domain,
    AvahiLookupResultFlags flags,
    void*                  userdata
) {
  (void)browser; (void)flags;

  ship_mdns_t* m = (ship_mdns_t*)userdata;

  switch (event) {
    case AVAHI_BROWSER_NEW:
      /* Skip our own service */
      if (strcmp(name, m->config.instance_name) == 0) break;

      EEBUS_LOG_DEBUG(LOG_TAG, "Found new SHIP node: '%s'", name);

      avahi_service_resolver_new(
          m->client, iface, proto, name, type, domain,
          AVAHI_PROTO_UNSPEC, 0, resolve_callback, m
      );
      break;

    case AVAHI_BROWSER_REMOVE:
      EEBUS_LOG_INFO(LOG_TAG, "SHIP node removed: '%s'", name);
      if (m->on_removed) m->on_removed(name, m->userdata);
      break;

    case AVAHI_BROWSER_FAILURE:
      EEBUS_LOG_ERROR(LOG_TAG, "mDNS browser failure: %s",
                      avahi_strerror(avahi_client_errno(m->client)));
      avahi_simple_poll_quit(m->poll);
      break;

    default:
      break;
  }
}

/* ── Avahi client ────────────────────────────────────────────────────────── */

static void client_callback(
    AvahiClient*       client,
    AvahiClientState   state,
    void*              userdata
) {
  ship_mdns_t* m = (ship_mdns_t*)userdata;
  m->client      = client;

  switch (state) {
    case AVAHI_CLIENT_S_RUNNING:
      create_services(m);
      break;

    case AVAHI_CLIENT_FAILURE:
      EEBUS_LOG_ERROR(LOG_TAG, "Avahi client failure: %s",
                      avahi_strerror(avahi_client_errno(client)));
      avahi_simple_poll_quit(m->poll);
      break;

    case AVAHI_CLIENT_S_COLLISION:
    case AVAHI_CLIENT_S_REGISTERING:
      if (m->group) avahi_entry_group_reset(m->group);
      break;

    default:
      break;
  }
}

/* ── Background thread ───────────────────────────────────────────────────── */

static void mdns_thread(void* arg) {
  ship_mdns_t* m = (ship_mdns_t*)arg;
  EEBUS_LOG_DEBUG(LOG_TAG, "mDNS event loop started");
  avahi_simple_poll_loop(m->poll);
  EEBUS_LOG_DEBUG(LOG_TAG, "mDNS event loop exited");
}

/* ── Public API ──────────────────────────────────────────────────────────── */

eebus_err_t ship_mdns_create(
    ship_mdns_t**             mdns,
    const ship_mdns_config_t* config,
    ship_mdns_peer_found_cb   on_found,
    ship_mdns_peer_removed_cb on_removed,
    void*                     userdata
) {
  if (!mdns || !config) return EEBUS_ERR_INVALID;

  ship_mdns_t* m = EEBUS_NEW(ship_mdns_t);
  if (!m) return EEBUS_ERR_NO_MEMORY;

  memcpy(&m->config, config, sizeof(m->config));
  m->on_found   = on_found;
  m->on_removed = on_removed;
  m->userdata   = userdata;

  *mdns = m;
  return EEBUS_OK;
}

eebus_err_t ship_mdns_start(ship_mdns_t* mdns) {
  if (!mdns) return EEBUS_ERR_INVALID;
  if (mdns->started) return EEBUS_ERR_EXISTS;

  /* Create Avahi simple poll */
  mdns->poll = avahi_simple_poll_new();
  if (!mdns->poll) return EEBUS_ERR_INTERNAL;

  /* Create Avahi client */
  int avahi_err;
  mdns->client = avahi_client_new(
      avahi_simple_poll_get(mdns->poll),
      0,
      client_callback,
      mdns,
      &avahi_err
  );
  if (!mdns->client) {
    EEBUS_LOG_ERROR(LOG_TAG, "avahi_client_new failed: %s", avahi_strerror(avahi_err));
    avahi_simple_poll_free(mdns->poll);
    mdns->poll = NULL;
    return EEBUS_ERR_INTERNAL;
  }

  /* Start browsing for SHIP peers */
  mdns->browser = avahi_service_browser_new(
      mdns->client,
      AVAHI_IF_UNSPEC,
      AVAHI_PROTO_UNSPEC,
      SHIP_MDNS_SERVICE_TYPE,
      NULL,
      0,
      browse_callback,
      mdns
  );
  if (!mdns->browser) {
    EEBUS_LOG_ERROR(LOG_TAG, "avahi_service_browser_new failed");
    avahi_client_free(mdns->client);
    mdns->client = NULL;
    avahi_simple_poll_free(mdns->poll);
    mdns->poll = NULL;
    return EEBUS_ERR_INTERNAL;
  }

  /* Spin up background thread */
  if (eebus_thread_create(&mdns->thread, mdns_thread, mdns, "mdns", 0) != EEBUS_OK) {
    avahi_service_browser_free(mdns->browser);
    avahi_client_free(mdns->client);
    avahi_simple_poll_free(mdns->poll);
    mdns->browser = NULL;
    mdns->client  = NULL;
    mdns->poll    = NULL;
    return EEBUS_ERR_INTERNAL;
  }

  mdns->thread_running = true;
  mdns->started        = true;
  EEBUS_LOG_INFO(LOG_TAG, "mDNS started, advertising '%s' on port %u",
                 mdns->config.instance_name, mdns->config.port);
  return EEBUS_OK;
}

void ship_mdns_stop(ship_mdns_t* mdns) {
  if (!mdns || !mdns->started) return;

  EEBUS_LOG_INFO(LOG_TAG, "Stopping mDNS");
  avahi_simple_poll_quit(mdns->poll);

  if (mdns->thread_running) {
    eebus_thread_join(mdns->thread);
    mdns->thread_running = false;
  }

  if (mdns->browser) {
    avahi_service_browser_free(mdns->browser);
    mdns->browser = NULL;
  }
  if (mdns->group) {
    avahi_entry_group_free(mdns->group);
    mdns->group = NULL;
  }
  if (mdns->client) {
    avahi_client_free(mdns->client);
    mdns->client = NULL;
  }
  if (mdns->poll) {
    avahi_simple_poll_free(mdns->poll);
    mdns->poll = NULL;
  }
  mdns->started = false;
}

void ship_mdns_destroy(ship_mdns_t* mdns) {
  if (!mdns) return;
  if (mdns->started) ship_mdns_stop(mdns);
  EEBUS_FREE(mdns);
}

eebus_err_t ship_mdns_set_register(ship_mdns_t* mdns, bool register_flag) {
  if (!mdns) return EEBUS_ERR_INVALID;
  if (!mdns->started) return EEBUS_ERR_STATE;

  mdns->config.register_flag = register_flag;

  if (mdns->group) {
    avahi_entry_group_reset(mdns->group);
    create_services(mdns);
  }
  return EEBUS_OK;
}
