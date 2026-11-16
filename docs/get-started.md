<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Getting started with resflex-open-eebus-connector

resflex-open-eebus-connector is a C11 implementation of the EEBus protocol stack
(SHIP 1.0.1 + SPINE 1.3.0 subset) distributed as a static library
(`libeebus.a`). It enables home-energy devices — control boxes, EV chargers,
heat pumps, and photovoltaic inverters — to communicate over a local network
using TLS-encrypted WebSocket connections with mDNS discovery.

Four demo programs exercise the SDK in Energy Guard and Controllable System
roles for LPC and LPP. See [spec-coverage.md](./spec-coverage.md) for what is
and is not implemented.

---

## 1. System requirements

### 1.1 Supported platform

| Component | Requirement |
| --- | --- |
| **OS** | Linux (tested on Ubuntu 22.04 LTS) |
| **Architecture** | x86_64, arm64 |
| **Compiler** | GCC >= 11 or Clang >= 14 (C11; C++17 for tests) |
| **Build system** | CMake >= 3.16, Ninja |
| **Runtime** | glibc >= 2.17 |

Other POSIX systems (macOS, FreeBSD) may work with platform-layer adaptations.
Windows is not supported: the Linux platform layer uses POSIX threads,
`timerfd`, and Unix-domain conventions.

### 1.2 Required dependencies

| Dependency | Minimum version | Package name | Purpose |
| --- | --- | --- | --- |
| **libwebsockets** | 4.3.0 | `libwebsockets-dev` | WebSocket client/server |
| **cJSON** | 1.7.0 | `libcjson-dev` | JSON parsing/generation |
| **Avahi client** | 0.8 | `libavahi-client-dev` | mDNS service discovery |
| **OpenSSL** | 3.0 (or 1.1) | `libssl-dev` | TLS |
| **pthreads** | — | `libc6-dev` | Threading (glibc built-in) |
| **librt** | — | `libc6-dev` | POSIX timers (glibc < 2.17) |

CMake exposes `EEBUS_TLS_MBEDTLS`, but there is no mbedTLS backend source in
this tree. Use OpenSSL.

### 1.3 Optional dependencies

| Dependency | Purpose |
| --- | --- |
| **readline** | CLI line editing with history and tab-completion |
| **gcov** | Code coverage (`-DEEBUS_ENABLE_COVERAGE=ON`) |
| **Google Test** | Fetched automatically by CMake when tests are enabled |

---

## 2. Installing dependencies

### 2.1 Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libwebsockets-dev \
    libcjson-dev \
    libavahi-client-dev \
    libssl-dev \
    libreadline-dev
```

Verify:

```bash
cmake --version          # expected: >= 3.16
ninja --version          # expected: >= 1.10
pkg-config --modversion libwebsockets   # expected: >= 4.3.0
pkg-config --modversion libcjson        # expected: >= 1.7.0
pkg-config --modversion avahi-client    # expected: >= 0.8
openssl version          # expected: >= 3.0
```

### 2.2 macOS (Homebrew)

```bash
brew install cmake ninja pkg-config \
    libwebsockets cjson avahi openssl readline
```

Avahi is not native on macOS. The mDNS backend is `src/ship/mdns/ship_mdns.h`
(Avahi only in this tree).

### 2.3 Fedora / RHEL / CentOS

```bash
sudo dnf install -y \
    gcc gcc-c++ cmake ninja-build pkg-config \
    libwebsockets-devel cjson-devel avahi-client-devel \
    openssl-devel readline-devel
```

On CentOS 7 / RHEL 7, glibc < 2.17 requires `librt`.

---

## 3. Building the SDK

### 3.1 Quick start

```bash
git clone <repository-url> resflex-open-eebus-connector
cd resflex-open-eebus-connector

cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

ninja -C build
```

### 3.2 Build output

| Artifact | Path | Description |
| --- | --- | --- |
| `libeebus.a` | `build/src/libeebus.a` | Static SDK library |
| `eebus-controlbox` | `build/bin/eebus-controlbox` | Demo: LPC EG + LPP EG |
| `eebus-heatpump` | `build/bin/eebus-heatpump` | Demo: LPC CS |
| `eebus-evcharger` | `build/bin/eebus-evcharger` | Demo: LPC CS |
| `eebus-pvinverter` | `build/bin/eebus-pvinverter` | Demo: LPP CS |
| `eebus-unit-tests` | `build/tests/eebus-unit-tests` | Unit test binary |

### 3.3 Build options

| CMake option | Default | Description |
| --- | --- | --- |
| `EEBUS_BUILD_TESTS` | `ON` | Build unit tests |
| `EEBUS_BUILD_EXAMPLES` | `ON` | Build demo programs |
| `EEBUS_ENABLE_COVERAGE` | `OFF` | Enable gcov code coverage |
| `EEBUS_TLS_OPENSSL` | `ON` | Use OpenSSL for TLS (the implemented backend) |

Example — library only, no tests or demos:

```bash
cmake -B build -G Ninja \
    -DEEBUS_BUILD_TESTS=OFF \
    -DEEBUS_BUILD_EXAMPLES=OFF
```

### 3.4 Building for release

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

---

## 4. Running the demos

### 4.1 Overview

Each demo listens on a dedicated TCP port and registers itself with mDNS
(`_ship._tcp`). Peers discover each other and establish TLS-authenticated
WebSocket connections.

| Demo | Port | Role |
| --- | --- | --- |
| `eebus-controlbox` | 7711 | Energy Guard (LPC + LPP) |
| `eebus-heatpump` | 7713 | Controllable System (LPC) |
| `eebus-evcharger` | 7714 | Controllable System (LPC) |
| `eebus-pvinverter` | 7715 | Controllable System (LPP) |

### 4.2 First run

Each demo stores TLS credentials and trusted peer SKIs under
`~/.eebus/<demo-name>/`. On the first run, a self-signed certificate is
generated automatically.

```bash
# Terminal 1 — control box (Energy Guard)
./build/bin/eebus-controlbox

# Terminal 2 — heat pump (LPC Controllable System)
./build/bin/eebus-heatpump

# Terminal 3 — EV charger (LPC Controllable System)
./build/bin/eebus-evcharger
```

Add the PV inverter when testing production limits:

```bash
./build/bin/eebus-pvinverter
```

### 4.3 Pairing (trust establishment)

EEBus connections require mutual TLS authentication. The first time two demos
connect, the SDK enters a pairing handshake:

1. **mDNS discovery** — demos announce themselves via Avahi.
2. **TLS handshake** — each side presents its self-signed certificate; the
   certificate Subject Key Identifier (SKI) is the peer identity.
3. **Hello PENDING** — an unknown SKI pauses in `HELLO_PENDING`.
4. **Auto-accept window** — the control box opens a 60-second auto-accept
   window on startup. During that window, inbound unknown peers are trusted
   automatically.
5. **Manual pairing** — after the window closes, or for outbound connections,
   the demo prompts:

   ```text
   [CB] Pairing requested from peer SKI: A1B2C3D4E5F6...
   [CB] Accept this peer? [y/N]:
   ```

   Type `y` to trust the peer and complete the handshake.

Once trusted, the SKI is persisted under `~/.eebus/<demo-name>/trusted/`.
Later connections proceed to `HELLO_READY` without prompting.

### 4.4 Demo CLI commands

Demos provide a readline-based CLI (or `fgets` if readline is missing).

Shared commands:

| Command | Description |
| --- | --- |
| `quit` | Disconnect and exit |
| `trust list` | Show trusted peers |
| `trust accept <ski>` | Accept a pending pairing request |
| `trust reject <ski>` | Reject a pending pairing request |
| `trust revoke <ski>` | Remove a trusted peer |
| `trust mode [auto\|manual]` | Switch trust mode |
| `connect <IP>:<PORT>` | Manually connect to a peer |
| `monitor [on\|off]` | Toggle the status panel |
| `log [debug\|info\|off]` | Set log level at runtime |

Control box only:

| Command | Description |
| --- | --- |
| `device [<ship-id>]` | List or inspect remote devices |
| `lpc [limit\|failsafe\|show] ...` | Write or inspect consumption limits |
| `lpp [limit\|failsafe\|show] ...` | Write or inspect production limits |

Controllable-system extras:

| Demo | Command | Description |
| --- | --- | --- |
| heatpump | `power <W>` | Set simulated consumption |
| heatpump / evcharger / pvinverter | `cfg ...` | Show or set persisted CS failsafe / nominal values |
| evcharger | `ev [connect\|disconnect]` | Simulate EV plug state |
| pvinverter | `irr <W>` | Set simulated DC input |

### 4.5 Typical scenario

Control box limiting a heat pump and an EV charger:

```bash
./build/bin/eebus-controlbox
./build/bin/eebus-heatpump
./build/bin/eebus-evcharger
```

1. Start the three demos.
2. Wait for the auto-accept window (60 s) or accept each peer when prompted.
3. The control box discovers the devices via mDNS and completes SHIP.
4. SPINE discovery, bindings, and subscriptions run automatically.
5. The control box sends heartbeats and may write LPC limits.
6. Type `monitor on` to watch live state.

### 4.6 Command-line arguments

```bash
./build/bin/eebus-controlbox --port 7711
./build/bin/eebus-heatpump --trust auto
./build/bin/eebus-heatpump --port 7713
```

`--trust auto` accepts unknown peers without prompting. The default is manual
pairing after the startup auto-accept window.

---

## 5. Running tests

```bash
ninja -C build eebus-unit-tests
./build/tests/eebus-unit-tests

./build/tests/eebus-unit-tests --gtest_filter="ShipModelTest.*"
./build/tests/eebus-unit-tests --gtest_filter="AddressTest.*"
./build/tests/eebus-unit-tests --gtest_filter="LcCsTest.*"
```

| Category | Files | Description |
| --- | --- | --- |
| `common/*` | `error_test`, `vector_test` | Core utility types |
| `log/*` | `log_test` | Logging and sanitisation |
| `ship/*` | `model_test`, `connection_test`, `connection_intg_test` | SHIP wire format and handshake |
| `spine/*` | codec, device, router, node-management, RFE | SPINE protocol |
| `uc/*` | `lc_cs_*`, `lc_eg_*`, `lc_parse_test` | LPC/LPP state machines |

---

## 6. Project structure overview

```text
resflex-open-eebus-connector/
├── CMakeLists.txt          # Root build file
├── sources.cmake           # SDK source manifest
├── src/                    # SDK implementation
│   ├── common/             # Error codes, memory, vector
│   ├── log/                # Logging
│   ├── platform/linux/     # Thread, mutex, timer
│   ├── ship/               # SHIP 1.0.1
│   ├── spine/              # SPINE 1.3.0 subset + codecs
│   └── uc/                 # LPC / LPP (LoadControl CS and EG)
├── examples/               # Four demo programs + common helpers
├── tests/                  # Google Test unit tests
└── EEBUSspec/              # Specification PDFs and XSDs
```

---

## 7. Troubleshooting

### 7.1 Avahi not running

```bash
sudo systemctl status avahi-daemon
sudo systemctl start avahi-daemon
sudo systemctl enable avahi-daemon
```

### 7.2 mDNS not discovering peers

- Demos must be on the same subnet (mDNS is link-local).
- Allow UDP 5353 and the demo WebSocket ports (7711, 7713–7715).
- Fall back to a manual connect:

  ```text
  connect 192.168.1.100:7713
  ```

### 7.3 Connection rejected

```bash
rm -rf ~/.eebus/<demo-name>/trusted/
```

Or start with `--trust auto`.

### 7.4 Rebuild from scratch

```bash
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
```

### 7.5 Debug logging

```bash
./build/bin/eebus-controlbox
```

Then in the CLI: `log debug`. Demos also write a log file in the working
directory (for example `eebus-hp.log`). Levels are `debug`, `info`, `warn`,
`error`, and `fatal`.

---

## 8. Integrating the SDK into your own project

The CMake target `eebus` publishes the repository root as its public include
directory. Demo programs add per-layer include paths so headers can be
included by file name, as below.

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(CJSON REQUIRED libcjson)
pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)
pkg_check_modules(AVAHI REQUIRED avahi-client)
find_package(OpenSSL REQUIRED)

target_link_libraries(my_app PRIVATE
    /path/to/build/src/libeebus.a
    ${CJSON_LIBRARIES}
    ${LIBWEBSOCKETS_LIBRARIES}
    ${AVAHI_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread rt
)
target_include_directories(my_app PRIVATE
    /path/to/src/common
    /path/to/src/log
    /path/to/src/platform
    /path/to/src/ship
    /path/to/src/ship/tls
    /path/to/src/ship/connection
    /path/to/src/ship/mdns
    /path/to/src/ship/websocket
    /path/to/src/spine
    /path/to/src/spine/codec
    /path/to/src/uc
)
```

Headers (demo-style include path):

```c
#include "eebus_log.h"
#include "ship_node.h"
#include "spine_device.h"
#include "spine_router.h"
#include "uc_lpc_cs.h"      /* or uc_lpp_cs.h / uc_lpc_lpp_cs.h */
#include "uc_lpc_lpp_eg.h"  /* or uc_lpc_eg.h / uc_lpp_eg.h */
```

SHIP runs its own threads after `ship_node_start()`; there is no poll/service
call. Minimal CS-side sketch (error handling omitted):

```c
eebus_log_config_t log_cfg = EEBUS_LOG_CONFIG_DEFAULT;
log_cfg.min_level = EEBUS_LOG_INFO;
eebus_log_init(&log_cfg);

ship_node_config_t node_cfg = {0};
snprintf(node_cfg.instance_name, sizeof(node_cfg.instance_name), "my-device-01");
node_cfg.port = 7713;
node_cfg.callbacks.on_spine_data = my_spine_handler;
node_cfg.callbacks.on_connected = my_connected;
node_cfg.callbacks.on_disconnected = my_disconnected;
node_cfg.callbacks.userdata = &app;

ship_node_t* node = NULL;
ship_node_create(&node, &node_cfg);
ship_node_start(node);

spine_device_t* dev = NULL;
spine_device_create_local(&dev, "d:my:device", local_ski, "HeatPumpAppliance");

spine_router_t* router = NULL;
spine_router_create(&router, dev, my_send_fn, &app);

spine_addr_t e1;
spine_addr_make(&e1, "d:my:device", 1, 0);

uc_lpc_cs_config_t lpc_cfg = {0};
lpc_cfg.entity.device = dev;
lpc_cfg.entity.cs_entity_addr = e1;
lpc_cfg.entity.entity_type = "HeatPumpAppliance";
lpc_cfg.entity.heartbeat_interval_s = 55;
lpc_cfg.entity.on_limit_write = my_limit_write_cb;
lpc_cfg.lpc.initial_failsafe.power_limit_w = 5000.0;
lpc_cfg.lpc.initial_failsafe.duration_s = 7200;

uc_lpc_cs_t* lpc = NULL;
uc_lpc_cs_create(&lpc, &lpc_cfg);

/* application event loop — SHIP I/O is already running on SDK threads */

uc_lpc_cs_destroy(lpc);
spine_router_destroy(router);
spine_device_destroy(dev);
ship_node_stop(node);
ship_node_destroy(node);
eebus_log_deinit();
```

Working programs: `examples/heatpump/main.c`, `examples/evcharger/main.c`,
`examples/pvinverter/main.c`, `examples/controlbox/main.c`. Architecture and
API summaries: [project-design.md](./project-design.md).
