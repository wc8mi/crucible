# SPDX-License-Identifier: MIT
# Copyright (c) 2026 David Behnke
# Crucible's feature defaults, read after the board's and platform's config.mk and before config/features.mk, so a
# board's own setting or the command line still wins (CRUCIBLE.md). Nothing is removed from the tree: what Crucible
# doesn't ship is only off by default here, and upstream files stay untouched.

# Trunking (DMR Tier III, P25 trunked): not shipped. Off leaves the flash and RAM YSF needs on the smaller radios
CONFIG_DMR_TRUNK ?= n
CONFIG_P25_TRUNK ?= n
# ... and out of the maximal configuration, which CI builds with every switch on that the board supports
MAXIMAL_SKIP += DMR_TRUNK P25_TRUNK
