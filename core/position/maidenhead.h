// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// Maidenhead locators (docs/spec/position.md): "EN82", "EN82mn", "EN82mn47". Field A-R, square 0-9, subsquare A-X,
// extended square 0-9; letters in either case. Positions are 1e-7 degree, as in position.h
#ifndef MAIDENHEAD_H
#define MAIDENHEAD_H

#include <stdbool.h>
#include <stdint.h>

#define MAIDENHEAD_MAX	8u	// characters, without the terminator

// The center of the locator's square. False: not 4, 6 or 8 valid characters (spaces around it are ignored)
bool maidenhead_parse(const char *s, int32_t *lat_e7, int32_t *lon_e7);

// The square holding the position, `chars` long (4, 6 or 8; anything else is 6): "EN82mn". out holds chars + 1
void maidenhead_format(int32_t lat_e7, int32_t lon_e7, uint8_t chars, char *out);

#endif
