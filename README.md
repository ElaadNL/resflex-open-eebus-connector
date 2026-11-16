<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# resflex-open-eebus-connector

C11 implementation of the EEBus protocol stack as a static library (`libeebus.a`).
It lets home-energy devices talk to each other on a local network using
TLS-encrypted WebSockets and mDNS discovery.

This repository is part of the ElaadNL / FAN Residential Flexibility
initiative. The connector is developed by enjoyelec and
copyrighted by ElaadNL, licensed under Apache 2.0.

**Implemented specifications** (copies live under `EEBUSspec/`):

| Layer | Specification | Version | In this SDK |
| --- | --- | --- | --- |
| Transport | SHIP (Smart Home IP) | 1.0.1 | Handshake, mDNS, TLS, WebSocket |
| Application protocol | SPINE Protocol + Resource | 1.3.0 | Datagram, NodeManagement, and the feature codecs needed by LPC/LPP |
| Use case | Limitation of Power Consumption (LPC) | 1.0.0 | Energy Guard and Controllable System |
| Use case | Limitation of Power Production (LPP) | 1.0.0 | Energy Guard and Controllable System |

This is a **subset** of SPINE, not a full feature catalogue. Monitoring of Power
Consumption (MPC) is **not** implemented as a use-case module. See
[docs/spec-coverage.md](docs/spec-coverage.md) for the chapter-by-chapter map.

## Disclaimer

This software is an open reference implementation, developed under the
coordination of ElaadNL as part of the
[Residential Flexibility](https://elaad.nl/en/projects/residential-flexibility/) initiative. It is not a finished,
certified, or end-user product. Before publication, the code was reviewed for
security by the European Network for Cyber Security (ENCS); that review is a
snapshot in time, not a guarantee. Anyone who integrates this software into a
product does so at their own risk and is responsible for its secure integration,
ongoing security testing, regulatory compliance, and any certification of that
product. The software is provided "as is", without warranty, under the Apache
License 2.0.

Read the full [disclaimer](DISCLAIMER.md) before using this software.

---

## Demo programs

Four programs exercise the library in home-network roles:

| Binary | Port | Role |
| --- | --- | --- |
| `eebus-controlbox` | 7711 | LPC Energy Guard + LPP Energy Guard |
| `eebus-heatpump` | 7713 | LPC Controllable System |
| `eebus-evcharger` | 7714 | LPC Controllable System |
| `eebus-pvinverter` | 7715 | LPP Controllable System |

---

## Build (Linux)

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config \
    libwebsockets-dev libcjson-dev libavahi-client-dev libssl-dev libreadline-dev

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
```

Artifacts: `build/src/libeebus.a`, demos under `build/bin/`, tests at
`build/tests/eebus-unit-tests`.

Full platform notes, pairing, CLI, and SDK integration:
[docs/get-started.md](docs/get-started.md).

---

## Documentation

| Document | Contents |
| --- | --- |
| [docs/overview.md](docs/overview.md) | What this connector is and how the stack fits together |
| [docs/spec-coverage.md](docs/spec-coverage.md) | EEBus spec chapters vs code |
| [docs/get-started.md](docs/get-started.md) | Build, demos, tests, integration |
| [docs/use-cases.md](docs/use-cases.md) | LPC/LPP scenarios and mapping to flexibility goals |
| [docs/project-design.md](docs/project-design.md) | Architecture, state machines, public APIs |
| [docs/glossary.md](docs/glossary.md) | EEBus terms (cross-protocol table in the appendix) |
| [docs/style-guide.md](docs/style-guide.md) | Documentation conventions |

---

## License

All project content is licensed under the [Apache 2.0 License](./LICENSE).

EEBus specification PDFs and XSDs under `EEBUSspec/` remain copyright of
EEBus Initiative e.V. They are included for implementer reference only.

---

## License compliance (REUSE)

This repository follows the [REUSE Specification](https://reuse.software/).
CI enforces REUSE on pull requests. See [CONTRIBUTING.md](./CONTRIBUTING.md).

---

## Questions

Open a GitHub issue.
