<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 David Behnke -->

# Crucible decision records

One file per decision that shapes Crucible: what was decided, why, what else was considered, and what it costs. A
record's supporting material (prototypes, screens) goes in a folder with the same number.

| No. | Decision | Status |
|---|---|---|
| [001](001-installer-two-paths.md) | The installer page: two paths, a work card, plain words | Accepted (amendment 1; revision 1: first install removed) |
| [002](002-tx-level-on-air.md) | TX level: one on-air target, a trim per radio | Superseded by 003 |
| [003](003-digital-agc.md) | Digital AGC: two switches, defaults per radio | Accepted |
| [004](004-nxdn-radio-default.md) | NXDN radio default: stored, 4800 bit/s, a quick rate switch | Proposed |
| [005](005-ysf-vw-vd1.md) | YSF VW and V/D mode 1: extend our engine, take the fork's self-contained parts | Accepted |

## How to add one

1. Copy the headings of the latest record. Take the next number: `002-short-name.md`, material in `002/`.
2. Status is one of **Proposed**, **Accepted**, **Superseded by NNN** or **Rejected**. The maintainer (KF8S) accepts.
3. Do not rewrite an accepted record. Write a new one that supersedes it.
4. Write in the house style: short sentences, tables, ASD-STE100 for anything a user reads.
