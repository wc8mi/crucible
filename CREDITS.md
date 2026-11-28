<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (c) 2026 David Behnke -->

# Credits

Crucible takes the best of the open radio firmware around it and says where each part came from. It is
GPL-3.0-or-later as a whole ([LICENSE](LICENSE)). Every file keeps the SPDX and copyright lines of the people who
wrote it, and this page names each project Crucible builds on or takes code from.

## Projects in this tree

| Project | Authors | License | What Crucible uses |
|---|---|---|---|
| [RadioFoundry](https://github.com/radiofoundry/radiofoundry) | The RadioFoundry Authors ([AUTHORS](AUTHORS), [CONTRIBUTORS](CONTRIBUTORS)) | MIT ([LICENSE-MIT](LICENSE-MIT)) | The whole base: core, drivers, platforms, boards, site, tools. Merged from `upstream/main` |
| [DM32-nxdn-p25](https://github.com/donttellmeimcute/DM32-nxdn-p25) | lyra XE1POC | GPL-3.0-or-later ([LICENSE](LICENSE)) | `core/fec/imbe88.{c,h}` (MIT, The RadioFoundry Authors: P25's voice code words moved out of `p25_unit.c`); the YSF VW voice channel interleave in `core/ysf/ysf_vw.c`, which the fork gives as the permutation of MMDVMHost (GPL-2.0-or-later) and DSD-FME (GPL-2.0-or-later); the NXDN repeater access design of its `digital-repeater-interop` patch: a lead-in of the reference pattern before the preamble (200 ms, its value) and the unit ID falling back to the radio's own (`air_session_tx_lead_in` and `nxdn_radio_unit_id` written here, [ADR 007](docs/crucible/adr/007-nxdn-conventional.md)) |
| [codec2-fixed-point](https://github.com/dbehnke/codec2-fixed-point) | David Behnke, from Codec 2 and Codec2-mod (below) | LGPL-2.1-only (its `LICENSE`) | The Codec 2 3200 encoder and decoder for M17, integer only: the submodule `third_party/codec2-fixed-point` (v0.1.1), built from source into the firmware when `CONFIG_M17` is on |
| [Codec 2](https://github.com/drowe67/codec2) | David Rowe and contributors | LGPL-2.1-only | The codec codec2-fixed-point is made from; each of its files keeps David Rowe's copyright lines |
| [Codec2-mod](https://github.com/M17-Project/Codec2-mod) | Wojciech Kaczmarski SP5WWP and the M17 Project | LGPL-2.1-only | The 3200-only refactor of Codec 2 that codec2-fixed-point starts from |
| [libm17](https://github.com/M17-Project/libm17) | Wojciech Kaczmarski SP5WWP and the M17 Project | GPL-2.0-or-later | No code: the M17 test vectors in `tests/vectors/m17-frames.txt` were made with its encoder (v1.4.3) on a host and are committed as data. Neither linked nor shipped |

Third-party libraries and fonts (FreeRTOS, TinyUSB, the STM32 HAL and CMSIS, the fonts) keep their own licenses, in
[LICENSES/](LICENSES/), in each submodule and in `platform/<platform>/THIRD_PARTY`.

## Adding a source

When code from another project comes in, the same commit:

1. keeps the SPDX and copyright lines of every file or part taken ([AGENTS.md](AGENTS.md) rule 3);
2. adds a row above: the project and its link, its authors as they credit themselves, its license and what was taken
   (files, or a feature and its commit);
3. adds the license text to [LICENSES/](LICENSES/) if it is new.

Only GPL-3.0-compatible code comes in ([AGENTS.md](AGENTS.md) rule 1).
