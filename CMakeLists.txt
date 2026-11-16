# EEBus SDK - Root CMake configuration
#
# Copyright 2025 EEBus SDK Contributors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.16)

project(eebus
  VERSION 1.0.0
  DESCRIPTION "EEBus protocol SDK - SHIP 1.0.1 / SPINE 1.3.0"
  LANGUAGES C CXX
)

# ── C standard ────────────────────────────────────────────────────────────────
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_BUILD_TYPE Debug)

# ── Build options ──────────────────────────────────────────────────────────────
option(EEBUS_BUILD_TESTS    "Build unit tests"     ON)
option(EEBUS_BUILD_EXAMPLES "Build examples"       ON)
option(EEBUS_ENABLE_COVERAGE "Enable gcov coverage" OFF)
option(EEBUS_TLS_OPENSSL    "Use OpenSSL for TLS"  ON)
option(EEBUS_TLS_MBEDTLS    "Use mbedTLS for TLS"  OFF)

# ── Compiler warnings ──────────────────────────────────────────────────────────
add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -Wno-unused-parameter
)

# ── Feature test macros ────────────────────────────────────────────────────────
# Define _GNU_SOURCE as empty (not =1) to match the bare "#define _GNU_SOURCE"
# in source files, avoiding -Wmacro-redefined / -Werror on strict environments
# (e.g. CentOS 7 / RHEL 7).  glibc tests with #ifdef, not #if, so an empty
# definition is functionally identical to =1 and enables all GNU/POSIX
# extensions including pthread_rwlock_t (needed by libuv / libwebsockets).
add_compile_definitions($<$<COMPILE_LANGUAGE:C>:_GNU_SOURCE=>)

# ── Coverage ───────────────────────────────────────────────────────────────────
if(EEBUS_ENABLE_COVERAGE)
  add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
  add_link_options(--coverage)
endif()

# ── Dependencies ───────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)

# libwebsockets
pkg_check_modules(LIBWEBSOCKETS REQUIRED IMPORTED_TARGET libwebsockets)

# cJSON
pkg_check_modules(CJSON REQUIRED IMPORTED_TARGET libcjson)

# TLS
if(EEBUS_TLS_OPENSSL)
  find_package(OpenSSL REQUIRED)
  add_compile_definitions(EEBUS_TLS_BACKEND_OPENSSL)
endif()

if(EEBUS_TLS_MBEDTLS)
  pkg_check_modules(MBEDTLS REQUIRED IMPORTED_TARGET mbedtls)
  add_compile_definitions(EEBUS_TLS_BACKEND_MBEDTLS)
endif()

if(NOT EEBUS_TLS_OPENSSL AND NOT EEBUS_TLS_MBEDTLS)
  message(FATAL_ERROR "At least one TLS backend must be enabled (EEBUS_TLS_OPENSSL or EEBUS_TLS_MBEDTLS)")
endif()

# Avahi (mDNS)
pkg_check_modules(AVAHI REQUIRED IMPORTED_TARGET avahi-client)

# ── Library target ─────────────────────────────────────────────────────────────
add_library(eebus STATIC)

include(sources.cmake)

target_include_directories(eebus
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(eebus
  PUBLIC
    PkgConfig::LIBWEBSOCKETS
    PkgConfig::CJSON
    PkgConfig::AVAHI
    pthread
    # librt needed on glibc < 2.17 (CentOS 7 / RHEL 7) for timer_settime
    rt
)

if(EEBUS_TLS_OPENSSL)
  target_link_libraries(eebus PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

if(EEBUS_TLS_MBEDTLS)
  target_link_libraries(eebus PUBLIC PkgConfig::MBEDTLS)
endif()

# Prevent undefined symbols from hiding until link time of the final binary
target_link_options(eebus PRIVATE -Wl,--no-undefined)

# ── Subdirectories ─────────────────────────────────────────────────────────────
if(EEBUS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(EEBUS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
