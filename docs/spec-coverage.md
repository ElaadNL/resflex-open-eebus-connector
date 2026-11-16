<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Specification coverage

This document maps `EEBUSspec/` to the code in this repository. It is the
authoritative statement of what resflex-open-eebus-connector implements.

Normative specifications remain copyright of EEBus Initiative e.V. The SHIP and
SPINE specification PDFs and the LPC/LPP high-level test specs are not
redistributed here; obtain them from the EEBus Initiative. The publicly
available use-case specifications and the XSD schemas are included under
`EEBUSspec/`. This page cites chapter numbers and requirement IDs only.
Presence of a file under `EEBUSspec/` does **not** mean it is implemented.

Status values:

| Status | Meaning |
| --- | --- |
| **Implemented** | Used on the LPC/LPP path |
| **Partial** | Protocol types or handshake exist; behaviour is restricted |
| **Codec only** | JSON encode/decode exists; no use-case module drives it |
| **Not implemented** | Spec is in the tree for reference only |

---

## 1. Specification set in this repository

| Path | Document | Version |
| --- | --- | --- |
| *obtain from EEBus Initiative* | SHIP | 1.0.1 |
| `EEBUSspec/ship/EEBus_SHIP_TS_TransferProtocol.xsd` | SHIP transfer protocol schema | 1.0.1 |
| *obtain from EEBus Initiative* | SPINE protocol | 1.3.0 |
| *obtain from EEBus Initiative* | SPINE resource model | 1.3.0 |
| `EEBUSspec/spine/XSDs/` | SPINE feature XSDs | 1.3.0 |
| `EEBUSspec/uc/EEBus_UC_TS_LimitationOfPowerConsumption_V1.0.0_public.pdf` | LPC | 1.0.0 |
| `EEBUSspec/uc/EEBus_UC_TS_LimitationOfPowerProduction_V1.0.0_public.pdf` | LPP | 1.0.0 |
| *obtain from EEBus Initiative* | LPC high-level tests | 1.0.0 |
| *obtain from EEBus Initiative* | LPP high-level tests | 1.0.0 |
| `EEBUSspec/uc/EEBus_UC_TS_MonitoringOfPowerConsumption_V1.0.0_public.pdf` | MPC | 1.0.0 |

MPC is **not** implemented as a use-case module. The PDF is retained as
reference. The SPINE Measurement codec remains because LPC/LPP and a future
measurement use case share those types.

---

## 2. SHIP 1.0.1

Code: `src/ship/`. Wire types: `src/ship/model/ship_model.h`. Handshake:
`src/ship/connection/ship_connection.h` (SHIP §13.4).

| SHIP chapter | Topic | Status | Code |
| --- | --- | --- | --- |
| §4 | Architecture / node parameters | Implemented | `ship_node` |
| §5–6 | Registration, reconnection | Implemented | `ship_node` reconnect with backoff |
| §7 | mDNS-SD `_ship._tcp`, TXT records | Implemented | `ship_mdns_avahi.c` |
| §8 | TCP | Implemented | via libwebsockets |
| §9 | TLS, SKI as node identity | Implemented (OpenSSL) | `ship_tls_openssl.c` |
| §10 | WebSocket `/ship/` | Implemented | `ship_ws_lws.c` |
| §11 | JSON control messages | Implemented | `ship_model.c` |
| §12 | Certificates, SKI, trust / auto-accept | Implemented | `ship_node` trust whitelist |
| §13.4.3 | CMI | Implemented | `ship_connection` |
| §13.4.4.1 | Hello pending / ready / aborted | Implemented | `ship_connection` |
| §13.4.4.2 | Protocol handshake JSON-UTF8 | Implemented | `ship_connection` |
| §13.4.5 | PIN | Partial | This node always sends `pinState=none`. Peer `required`/`optional` is parsed; this SDK does not drive PIN entry |
| §13.4.6 | Access methods | Implemented | `ship_connection` |
| §13.4.7 | Connection close | Implemented | `ship_connection` |
| §12.2.2 | Duplicate-connection handling | Implemented | `ship_node` |

Intentionally not claimed:

- mbedTLS backend (`EEBUS_TLS_MBEDTLS` exists in CMake; no `ship_tls_mbedtls.c`)
- PIN-based pairing UI
- Non-Avahi mDNS backends

---

## 3. SPINE protocol 1.3.0

Code: `src/spine/` plus `src/spine/codec/`.

| Protocol topic | Spec | Status | Code |
| --- | --- | --- | --- |
| Datagram header, address, msgCounter, cmdClassifier | Protocol §5 | Implemented | `spine_codec_datagram`, `spine_codec_msg`, `spine_codec_address` |
| Result | Protocol / Resource §3.11 | Implemented | `spine_codec_result` |
| Restricted Function Exchange (RFE) | Resource §3.6 | Implemented | `spine_rfe` |
| NodeManagement DetailedDiscovery | Protocol / NM | Implemented | `spine_node_mgmt`, `spine_codec_node_mgmt` |
| NodeManagement UseCaseData | NM | Implemented | same |
| Binding management | Binding XSD | Implemented | `spine_sub_bind`, `spine_codec_sub_bind` |
| Subscription management | Subscription XSD | Implemented | same |
| Device / entity / feature model | Protocol §3 | Implemented (local + remote mirror) | `spine_device` |
| Message routing by SHIP session | — | Implemented | `spine_router` |

Discovery after SHIP DATA_EXCHANGE: DetailedDiscovery READ/REPLY, UseCase
READ/REPLY, then bind and subscribe. See [project-design.md](./project-design.md)
section 4.

---

## 4. SPINE resource 1.3.0 — implemented features

Only features required by LPC/LPP (plus Measurement as codec) are present.

| Feature / function | Resource / XSD | Status | Codec |
| --- | --- | --- | --- |
| `loadControlLimitListData` | LoadControl §5.3.14.5 | Implemented | `spine_codec_load_ctrl` |
| `loadControlLimitDescriptionListData` | LoadControl §5.3.14.7 | Implemented | `spine_codec_load_ctrl` |
| `deviceConfigurationKeyValueListData` (+ description) | DeviceConfiguration | Implemented | `spine_codec_dev_cfg` |
| `deviceDiagnosisHeartbeatData` | DeviceDiagnosis | Implemented | `spine_codec_dev_diag` |
| `deviceDiagnosisStateData` | DeviceDiagnosis | Implemented | `spine_codec_dev_diag` |
| `electricalConnectionDescriptionListData` | ElectricalConnection §5.3.11.1 | Implemented | `spine_codec_elec_conn` |
| `electricalConnectionParameterDescriptionListData` | §5.3.11.2 | Implemented | `spine_codec_elec_conn` |
| `electricalConnectionPermittedValueSetListData` | §5.3.11.3 | Implemented | `spine_codec_elec_conn` |
| `electricalConnectionCharacteristicListData` | §5.3.11.6 | Implemented | `spine_codec_elec_conn` |
| `measurementListData` | Measurement | Codec only | `spine_codec_measurement` |
| `measurementDescriptionListData` | Measurement | Codec only | `spine_codec_measurement` |
| Common types (`ScaledNumber`, `TimePeriod`, …) | Resource §3.10 | Implemented | `spine_codec_common.h` |

---

## 5. SPINE resource — not implemented

XSDs exist under `EEBUSspec/spine/XSDs/`. There is no codec or use-case code
for:

- Alarm, ActuatorLevel, ActuatorSwitch
- Bill, DataTunneling, DirectControl
- DeviceClassification, HVAC, Identification
- IncentiveTable, Messaging, NetworkManagement
- OperatingConstraints, PowerSequences, Sensing
- Setpoint, SmartEnergyManagementPs, StateInformation
- SupplyCondition, TariffInformation, TaskManagement
- Threshold, TimeInformation, TimeSeries, TimeTable, Version

This is intentional. The SDK is an LPC/LPP connector, not a general SPINE
stack.

---

## 6. LPC 1.0.0 and LPP 1.0.0

LPC and LPP share one implementation (`src/uc/uc_lc*.c`) with consume and
produce directions. Application code includes the thin wrappers
`uc_lpc_cs.h` / `uc_lpp_cs.h` / `uc_lpc_lpp_cs.h` (CS) and
`uc_lpc_eg.h` / `uc_lpp_eg.h` / `uc_lpc_lpp_eg.h` (EG).

| Scenario | Spec | Requirement IDs (examples) | Status |
| --- | --- | --- | --- |
| 1 — Active power limit | LPC/LPP §2.6.1 | LPC-011 / LPP-011 | Implemented |
| 2 — Failsafe power and duration | §2.6.2 | LPC-021, LPC-022 (duration 2 h–24 h, shared across directions) | Implemented |
| 3 — Heartbeat | §2.6.3 | LPC-031; 120 s timeout into FAILSAFE | Implemented |
| 4 — Constraints (nominal / contractual max) | §2.6.4 | LPC-041 | Implemented |
| CS state machine | §2.3 | LPC-906 … LPC-922 | Implemented (`lc_cs_state_t`) |
| Actors EG and CS | §2.5 | — | Implemented |
| Cascading constellation (device is both CS and EG) | §2.4 | — | Not demonstrated. An EG may still control several remote CS devices (control box demo) |
| Dependency on MPC / MGCP / Monitoring of Inverter | §2.7 | — | Not implemented |

High-level test specs (LPC and LPP; obtain from the EEBus Initiative) are not
executed as a separate test harness. Unit tests under
`tests/uc/` cover CS/EG state machines, dispatch, and EG cache behaviour.

---

## 7. MPC 1.0.0

| Item | Status |
| --- | --- |
| UC TS PDF in `EEBUSspec/uc/` | Present (reference only) |
| `uc_mpc_ma` / `uc_mpc_mu` modules | Removed |
| Measurement codec | Kept (`spine_codec_measurement`) |
| Demo MPC roles | Removed |

Do not include `uc_mpc_*.h`. There is no Monitoring Appliance or Measurement
Unit API in this tree.

---

## 8. Demo mapping

| Program | Port | Spec roles |
| --- | --- | --- |
| `eebus-controlbox` | 7711 | LPC EG + LPP EG |
| `eebus-heatpump` | 7713 | LPC CS |
| `eebus-evcharger` | 7714 | LPC CS |
| `eebus-pvinverter` | 7715 | LPP CS |

There is no HEMS demo. Cascading LPC/LPP (§2.4) is therefore not shown as a
combined CS+EG node.

---

## 9. Related documentation

- Stack and roles: [overview.md](./overview.md)
- Scenario narrative: [use-cases.md](./use-cases.md)
- State machines and APIs: [project-design.md](./project-design.md)
