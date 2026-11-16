<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Documentation style guide

This guide provides conventions for documentation in this repository.

---

## File Naming

- Use **lowercase with dashes** for all filenames:

  ```text
  good: protocol-overview.md
  bad: ProtocolOverview.md or protocol_overview.md
  ```

- Avoid special characters, spaces, or abbreviations that aren't common across the project.

---

## Headings

- Use ATX-style headings (`#`, `##`, `###`)
- Start each document with a single `#` title
- Use sentence case for headings:

  ```text
  ✅ Connector configuration
  ❌ Connector Configuration
  ```

---

## Language

- Use clear, neutral English
- Avoid unnecessary abbreviations (e.g. write “Home Energy Management System (HEMS)” once, then use HEMS)
- Write in the present tense and active voice where possible

---

## Terminology

- Use shared terms from the [glossary](./glossary.md)
- Be consistent with protocol naming:
  - ✅ “OCPP 2.1”, “EEBUS (SHIP/SPINE)”, “Matter 1.4.1”
  - ❌ “OcCp”, “ee-bus”, or “S2 Protocol” (redundant phrasing)

---

## Formatting

- Use fenced code blocks with language tag (` ```json`, ` ```bash`, etc.)
- Limit lines to 120 characters where possible (not enforced)
- Use lists and tables for structure when appropriate

---

## Markdown Linting

All documentation is checked automatically using [markdownlint](https://github.com/DavidAnson/markdownlint).  
A shared configuration file `.markdownlint.yaml` is used across all repositories. You can run linting locally or rely on GitHub Actions.

---

## Directory layout

This repository uses:

```text
/
├── README.md
├── CONTRIBUTING.md
├── SECURITY.md
├── docs/
│   ├── index.md
│   ├── overview.md
│   ├── spec-coverage.md
│   ├── get-started.md
│   ├── use-cases.md
│   ├── project-design.md
│   ├── glossary.md
│   └── style-guide.md
└── .github/
    └── workflows/
```

---

## When in doubt

When unsure about naming or formatting, follow an existing file in `docs/`
or open a GitHub issue.
