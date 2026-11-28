// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Behnke
// The firmware's name as users see it: the boot splash, About, the USB manufacturer string, network banners. Crucible
// is a downstream build of RadioFoundry (CRUCIBLE.md); internal names (rfoundry_*, RFOUNDRY_*, the protocols) stay
// RadioFoundry's. A build may override any of these with -D.
#ifndef RFOUNDRY_PRODUCT_H
#define RFOUNDRY_PRODUCT_H

#ifndef RFOUNDRY_PRODUCT
#define RFOUNDRY_PRODUCT		"Crucible"
#endif
// The splash wordmark: the first part in the accent color, the second in the text color
#ifndef RFOUNDRY_WORDMARK_ACCENT
#define RFOUNDRY_WORDMARK_ACCENT	"Crucible"
#endif
#ifndef RFOUNDRY_WORDMARK_REST
#define RFOUNDRY_WORDMARK_REST		""
#endif
// About's License: line. Crucible as a whole is GPL-3.0-or-later; the RadioFoundry files keep their MIT notice
#ifndef RFOUNDRY_LICENSE_LINE
#define RFOUNDRY_LICENSE_LINE		"GPL-3.0-or-later. RadioFoundry parts MIT, (c) 2026 The RadioFoundry Authors"
#endif
// 1: the splash draws the mark in core/ui/product_mark.h instead of RadioFoundry's hexagon
#ifndef RFOUNDRY_PRODUCT_MARK
#define RFOUNDRY_PRODUCT_MARK		1
#endif

#endif
