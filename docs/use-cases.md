<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Use cases

This connector implements two EEBus use cases:

- **LPC 1.0.0** — Limitation of Power Consumption
- **LPP 1.0.0** — Limitation of Power Production

Both share the LoadControl implementation in `src/uc/` (consume vs produce
direction). Specification copies: `EEBUSspec/uc/`. Chapter-level coverage:
[spec-coverage.md](./spec-coverage.md).

---

## Actors

| Actor | Spec | In this repository |
| --- | --- | --- |
| Energy Guard (EG) | LPC/LPP §2.5.1 | `eebus-controlbox` |
| Controllable System (CS) | LPC/LPP §2.5.2 | `eebus-heatpump`, `eebus-evcharger` (LPC); `eebus-pvinverter` (LPP) |

The EG discovers a CS, reads descriptions and current values, then writes
limits. The CS applies the limit and watches the EG heartbeat.

---

## Scenarios (LPC and LPP)

The two specifications are symmetric. LPC numbers are cited; LPP has matching
requirements.

### Scenario 1 — Active power limit

The EG writes `loadControlLimitListData` with an active power cap
(watts) and optional validity period (`timePeriod`). The CS moves to
**LIMITED** while the limit is active (LPC-011 / LPP-011).

### Scenario 2 — Failsafe values

If the CS misses EG heartbeats for more than 120 seconds, it applies the
failsafe power limit and stays in **FAILSAFE** for at least the configured
duration (default vendor range 2 h–24 h, LPC-021 / LPC-022). The EG may
update failsafe power and duration. In combined LPC+LPP, duration is shared
across directions (LPC-022).

### Scenario 3 — Heartbeat

The EG sends `deviceDiagnosisHeartbeatData` on an interval of at most 60 s
(LPC-006). The CS treats a 120 s gap as loss of control (LPC-031).

### Scenario 4 — Constraints

The CS exposes a nominal (or contractual, for CEM-class devices) maximum
power via ElectricalConnection characteristics (LPC-041). The EG reads this
before choosing a limit.

---

## Controllable System states

From LPC/LPP §2.3, implemented as `lc_cs_state_t`:

| State | Meaning |
| --- | --- |
| `INIT` | After start; waiting for heartbeat and a first limit write |
| `UNLIMITED_CONTROLLED` | EG in control, no active limit |
| `LIMITED` | EG set an active limit |
| `FAILSAFE` | Heartbeat lost; failsafe power applied |
| `UNLIMITED_AUTONOMOUS` | No EG control; device runs on its own |

After `on_cs_ready`, the EG application must write a limit (active or
inactive) within 60 s (LPC-913), otherwise the CS init watchdog (120 s)
moves the CS to `UNLIMITED_AUTONOMOUS`.

State-machine diagram: [project-design.md](./project-design.md) section 5.3.

---

## What this connector does not implement

- **MPC** (Monitoring of Power Consumption) — no MA/MU module
- **Dynamic tariffs** — no TariffInformation / IncentiveTable features
- **Cascading CS+EG node** — LPC/LPP §2.4 is not demonstrated (the former
  HEMS demo was removed)
- **DSO capacity-profile ingest** — that sits above the connector, in the
  HEMS or grid application that *uses* this EG API

---

## Mapping to flexibility goals

Residential flexibility has three protocol-agnostic household goals.
This connector supports the *device-side limit channel*, not the full HEMS
optimisation loop.

| Goal | How LPC/LPP help | Gap |
| --- | --- | --- |
| Limiting peak grid demand | LPC writes a consumption cap (and failsafe) to chargers, heat pumps, and similar CS devices | The DSO capacity envelope is not a SHIP/SPINE message in this SDK; the EG application must already know the cap |
| Dynamic tariff optimisation | Not supported | No tariff or schedule use case |
| Maximising self-consumption | LPP can cap feed-in so local load can take surplus; LPC can raise or lower consumption | No MPC, so this SDK does not publish live power measurements |

---

## Demos

Typical LPC path: start `eebus-controlbox` with `eebus-heatpump` and
`eebus-evcharger`. Typical LPP path: add `eebus-pvinverter`. Build and
pairing steps: [get-started.md](./get-started.md).
