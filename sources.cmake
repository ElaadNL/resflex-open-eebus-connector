# EEBus SDK source file manifest

target_sources(eebus
  PRIVATE
    # ── Common ────────────────────────────────────────────────────────────────
    src/common/eebus_error.c
    src/common/eebus_memory.c
    src/common/eebus_vector.c

    # ── Logging ───────────────────────────────────────────────────────────────
    src/log/eebus_log.c

    # ── Platform – Linux ──────────────────────────────────────────────────────
    src/platform/linux/eebus_thread_linux.c
    src/platform/linux/eebus_mutex_linux.c
    src/platform/linux/eebus_timer_linux.c

    # ── SHIP layer ────────────────────────────────────────────────────────────
    src/ship/ship_node.c
    src/ship/model/ship_model.c
    src/ship/connection/ship_connection.c   # requires libwebsockets
    src/ship/mdns/ship_mdns_avahi.c         # requires avahi
    src/ship/tls/ship_tls_openssl.c         # requires OpenSSL (or mbedTLS)
    src/ship/websocket/ship_ws_lws.c        # requires libwebsockets

    # ── SPINE codec layer ─────────────────────────────────────────────────────
    src/spine/codec/spine_codec_address.c
    src/spine/codec/spine_codec_msg.c
    src/spine/codec/spine_codec_datagram.c
    src/spine/codec/spine_codec_result.c
    src/spine/codec/spine_codec_load_ctrl.c
    src/spine/codec/spine_codec_dev_cfg.c
    src/spine/codec/spine_codec_dev_diag.c
    src/spine/codec/spine_codec_elec_conn.c
    src/spine/codec/spine_codec_node_mgmt.c
    src/spine/codec/spine_codec_sub_bind.c
    src/spine/codec/spine_codec_measurement.c
    src/spine/codec/spine_rfe.c

    # ── SPINE protocol layer ──────────────────────────────────────────────────
    src/spine/spine_device.c
    src/spine/spine_sub_bind.c
    src/spine/spine_node_mgmt.c
    src/spine/spine_router.c

    # ── Use cases ─────────────────────────────────────────────────────────────
    src/uc/uc_lc.c
    src/uc/uc_lc_cs.c
    src/uc/uc_lc_eg.c
)
