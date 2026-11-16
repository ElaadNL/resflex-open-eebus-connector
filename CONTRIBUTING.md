<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Contributing

Thank you for contributing to resflex-open-eebus-connector.

This repository is a C11 EEBus SDK (SHIP/SPINE, LPC/LPP), not a documentation
template. Contributions may be code, tests, examples, or documentation.

---

## How to contribute

1. Fork the repository and create a branch.
2. Keep SDK behaviour aligned with the cited EEBus chapters
   ([docs/spec-coverage.md](docs/spec-coverage.md)). Do not paste specification
   PDF text into the tree (EEBus Initiative copyright).
3. Add or update tests when changing protocol or use-case behaviour.
4. Follow existing C style (`.clang-format`, 120 columns).
5. Keep source files REUSE-compliant (SPDX headers). Markdown uses the HTML
   SPDX block already present in `docs/`.
6. Open a pull request. CI runs documentation lint and REUSE checks.

---

## Branch naming

- `fix/<topic>` — bug fixes
- `feat/<topic>` — features
- `docs/<topic>` — documentation only
- `test/<topic>` — tests

---

## Review

Maintainers review for:

- Correctness against SHIP / SPINE / LPC / LPP as implemented here
- Test coverage for behavioural changes
- Documentation accuracy (especially [spec-coverage.md](docs/spec-coverage.md)
  if the implemented subset changes)
- Style and REUSE compliance

---

## Code of conduct

Keep discussion respectful and technical.

No CLA is required at this time.

---

## Questions

Open a GitHub issue.
