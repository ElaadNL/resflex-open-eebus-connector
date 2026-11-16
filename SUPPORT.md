<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Support

This file explains what kind of support is available for this repository and how to get help if needed.

---

## Scope of Support

This repository is part of the ElaadNL Residential Flexibility initiative and contains
the EEBus connector: a C11 SHIP/SPINE SDK (`libeebus.a`) implementing the LPC and
LPP use cases.

Support in this context means:

- Help building the library and linking `libeebus.a` into a project
- Questions about SHIP pairing, SPINE discovery, and the LPC/LPP use cases as implemented here
- Suggestions on documentation, naming, and consistency

---

## Where to Ask Questions

- For questions, bugs, or suggestions: open a GitHub issue
- For what is and is not implemented: see [docs/spec-coverage.md](./docs/spec-coverage.md)
- For security issues: do not open an issue, follow [SECURITY.md](./SECURITY.md)

---

## What Is Not Supported

This repository does not provide:

- Interpretation of the EEBus specifications themselves; obtain them from the
  [EEBus Initiative](https://www.eebus.org/)
- A complete SPINE stack or unimplemented use cases (MPC, tariffs, …)
- Help with proprietary product integration
- Certification of any product (see [DISCLAIMER.md](./DISCLAIMER.md))
- Operational support or service level guarantees

---

## Notes

Support is provided on a best-effort, community basis. This does not prevent any
party from offering or selling commercial support separately.
