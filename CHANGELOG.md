<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

---

## [1.0.0] – 2026-11-16

Initial public release, developed under the ElaadNL
[Residential Flexibility](https://elaad.nl/en/projects/residential-flexibility/)
initiative.

### Added

- C11 EEBus protocol stack built as a static library (`libeebus.a`)
- SHIP 1.0.1 transport: mDNS-SD discovery, TLS, WebSocket, and the pairing
  handshake with SKI-based trust
- SPINE 1.3.0 datagram, NodeManagement (DetailedDiscovery and UseCase), and the
  feature codecs required by LPC/LPP
- Limitation of Power Consumption (LPC) 1.0.0 and Limitation of Power Production
  (LPP) 1.0.0 use cases, in the Energy Guard and Controllable System roles
- Four demo programs (control box, heat pump, EV charger, PV inverter) and
  host-side unit tests
