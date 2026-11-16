<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Glossary

Terms used in this connector. Prefer these names in documentation and code
comments.

| Term | Meaning in this project |
| --- | --- |
| **SHIP** | Smart Home IP (v1.0.1). Discovery, TLS identity, WebSocket transport. |
| **SPINE** | Smart Premises Interoperable Neutral-message Exchange (v1.3.0). Device/feature model and JSON datagrams. |
| **SKI** | Subject Key Identifier of the SHIP TLS certificate (40 hex characters). Peer identity. |
| **mDNS** | Multicast DNS-SD. SHIP service type `_ship._tcp`. |
| **HEMS** | Home Energy Management System. In EEBus also called CEM (Central Energy Manager). This SDK has no HEMS demo; a control box acts as Energy Guard. |
| **FEID** | Flexible energy-intensive device (charger, heat pump, inverter, …). |
| **Energy Guard (EG)** | LPC/LPP actor that writes limits, failsafe values, and heartbeats. |
| **Controllable System (CS)** | LPC/LPP actor that applies limits and failsafe. |
| **LPC** | Limitation of Power Consumption (UC 1.0.0). |
| **LPP** | Limitation of Power Production (UC 1.0.0). |
| **MPC** | Monitoring of Power Consumption. Specified under `EEBUSspec/uc/` but **not implemented** as a use-case module. |
| **Feature** | SPINE capability on an entity (LoadControl, DeviceDiagnosis, …). |
| **Entity** | SPINE grouping of features under a device (for example the CS entity). |
| **Binding** | Client–server association required before some writes (LoadControl, DeviceConfiguration). |
| **Subscription** | Client registration to receive NOTIFY for a server feature. |
| **Heartbeat** | `deviceDiagnosisHeartbeatData` from EG to CS (Scenario 3). |
| **Failsafe** | Fallback power limit and minimum duration when the heartbeat is lost (Scenario 2). |
| **Limit** | Active power cap in watts (`loadControlLimitListData`, Scenario 1). |
| **RFE** | Restricted Function Exchange — partial list writes/filters in SPINE. |
| **NodeManagement** | SPINE feature for discovery, use-case advertisement, bind, and subscribe. |

For requirement IDs (LPC-011, LPC-022, …) see the UC PDFs and
[spec-coverage.md](./spec-coverage.md).

---

## Appendix: cross-protocol terms

The following table maps terms across the open protocols. It is **not** an
API map for this SDK. It only helps when comparing protocols.

| Term | S2 | EEBus / SPINE | Matter 1.4 | Notes |
| --- | --- | --- | --- | --- |
| **HEMS** | CEM (Customer Energy Manager) | CEM (Central Energy Manager) | Controller (multi-admin Node) | Central orchestrator. |
| **Device / Asset** | RM (Resource Manager) | SPINE Actor (e.g. EVSE, PV) | Node (with Endpoints) | Flexible device. |
| **Function** | Control Type (e.g. PEBC) | Use Case + Data Point via SPINE Features | Endpoint (Device Type + Clusters) | Logical capability. |
| **Constraint** | PowerConstraints / EnergyConstraints | LoadControlLimitListData, LoadControlEventListData | DeviceEnergyManagementMode cluster | Operational or grid limits. |
| **Instruction** | PEBC.Instruction | LoadControlEventListData / ScheduleListData | Energy Management Schedule cluster | Setpoint or plan. |
| **Status** | InstructionStatusUpdate | LoadControlStateListData, Result objects | Attributes and telemetry | Feedback. |
| **Measurement** | PowerMeasurement / Forecast | MeasurementListData | ElectricalMeasurement cluster | This SDK has a Measurement *codec* only. |
| **Device Type** | SystemDescription | SPINE Actor Type | Device Type Library | Capability declaration. |
| **Topology** | CEM ↔ RM over s2-ws-json | SHIP + SPINE | Fabric with Nodes, Admins, Clusters | Control structure. |
| **Transport** | s2-ws-json (WebSocket/JSON) | SHIP over TLS WebSocket | Matter IP stack | This connector: SHIP. |
| **Security Domain** | Session with authentication | SHIP pairing and SKI trust | Fabric with ACLs and certificates | Trust boundary. |
| **Use Case** | ControlType + Forecast | SPINE Use Case Model | Cluster interaction | Here: LPC and LPP. |
| **Interop Layer** | S2 semantic model | SPINE data model | Matter data model (Clusters) | Message structure. |
