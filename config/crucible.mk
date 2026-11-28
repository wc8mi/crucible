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

# Not shipped either, to leave flash for YSF VW and M17 (flash saved, an ARM radio's default build): the MMDVM hotspot
# with split duplex (32 KB), the single frequency repeater, standalone and the hotspot's mode (10 KB), and zone sharing
# over DMR data (15 KB). CONFIG_<switch>=y on the command line builds one back
CONFIG_HOTSPOT ?= n
CONFIG_SFR ?= n
CONFIG_ZONE_SHARE ?= n
MAXIMAL_SKIP += HOTSPOT SFR ZONE_SHARE
