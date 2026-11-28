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

Third-party libraries and fonts (FreeRTOS, TinyUSB, the STM32 HAL and CMSIS, the fonts) keep their own licenses, in
[LICENSES/](LICENSES/), in each submodule and in `platform/<platform>/THIRD_PARTY`.

## Adding a source

When code from another project comes in, the same commit:

1. keeps the SPDX and copyright lines of every file or part taken ([AGENTS.md](AGENTS.md) rule 3);
2. adds a row above: the project and its link, its authors as they credit themselves, its license and what was taken
   (files, or a feature and its commit);
3. adds the license text to [LICENSES/](LICENSES/) if it is new.

Only GPL-3.0-compatible code comes in ([AGENTS.md](AGENTS.md) rule 1).
