# examples/CMakeLists.txt
#
# Builds the four EEBus home-network demo programs:
#
#   eebus-controlbox  — LPC EG + LPP EG                    (port 7711)
#   eebus-heatpump    — LPC CS                             (port 7713)
#   eebus-evcharger   — LPC CS                             (port 7714)
#   eebus-pvinverter  — LPP CS                             (port 7715)

cmake_minimum_required(VERSION 3.16)

# ── Common demo library ────────────────────────────────────────────────────────

add_library(eebus_demo_common STATIC
  common/tls_persist.c
  common/trust.c
  common/ship_helper.c
  common/cli.c
  common/dashboard.c
  common/device_table.c
  common/failsafe_persist.c
  common/cs_config.c
)

target_include_directories(eebus_demo_common PUBLIC
  ${CMAKE_SOURCE_DIR}/src/common
  ${CMAKE_SOURCE_DIR}/src/log
  ${CMAKE_SOURCE_DIR}/src/platform
  ${CMAKE_SOURCE_DIR}/src/ship
  ${CMAKE_SOURCE_DIR}/src/ship/tls
  ${CMAKE_SOURCE_DIR}/src/ship/connection
  ${CMAKE_SOURCE_DIR}/src/ship/mdns
  ${CMAKE_SOURCE_DIR}/src/ship/websocket
  ${CMAKE_SOURCE_DIR}/src/spine
  ${CMAKE_SOURCE_DIR}/src/spine/codec
  ${CMAKE_SOURCE_DIR}/src/uc
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)

# Use the same target names as the root CMakeLists.txt:
#   eebus                   — SDK library (already built)
#   OpenSSL::SSL/Crypto     — from find_package(OpenSSL) in root
#   PkgConfig::LIBWEBSOCKETS — from pkg_check_modules(LIBWEBSOCKETS ...) in root
#   PkgConfig::AVAHI        — from pkg_check_modules(AVAHI ...) in root
#   pthread                 — plain linker flag, no find_package needed
target_link_libraries(eebus_demo_common PUBLIC
  eebus
  OpenSSL::SSL
  OpenSSL::Crypto
  PkgConfig::LIBWEBSOCKETS
  PkgConfig::AVAHI
  pthread
  m
  # librt is needed on glibc < 2.17 (e.g. CentOS 7 / RHEL 7) for
  # timer_settime / timer_create used by eebus_timer_linux.c.
  # On glibc >= 2.17 these symbols live in libc and -lrt is a no-op.
  rt
)

# Optional readline (graceful fallback to fgets if not present)
find_library(READLINE_LIB readline)
if(READLINE_LIB)
  target_compile_definitions(eebus_demo_common PUBLIC HAVE_READLINE)
  target_link_libraries(eebus_demo_common PUBLIC ${READLINE_LIB})
  message(STATUS "[demo] readline found — line editing enabled")
else()
  message(STATUS "[demo] readline not found — using fgets fallback")
endif()

# ── Helper macro ───────────────────────────────────────────────────────────────

macro(add_demo NAME)
  add_executable(eebus-${NAME} ${NAME}/main.c)
  target_link_libraries(eebus-${NAME} PRIVATE eebus_demo_common)
  set_target_properties(eebus-${NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  )
  install(TARGETS eebus-${NAME} RUNTIME DESTINATION bin OPTIONAL)
endmacro()

# ── Four demo programs ─────────────────────────────────────────────────────────

add_demo(controlbox)
add_demo(heatpump)
add_demo(evcharger)
add_demo(pvinverter)
