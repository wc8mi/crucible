// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// Maidenhead locators (maidenhead.h). Both ways count in the smallest square, the extended one: 1/120 degree of
// longitude by 1/240 degree of latitude, 43200 of them each way, so the arithmetic is whole numbers

#include "maidenhead.h"
#include <stddef.h>

#define MH_CELLS	43200u		// extended squares around the world, and pole to pole
#define MH_LON_E7	1800000000u	// 180 degrees
#define MH_LAT_E7	900000000u

// The value of one locator character at position k (0..7) of a pair, or -1
static int mh_digit(char c, unsigned pair)
{
	if (pair == 1u || pair == 3u)
		return c >= '0' && c <= '9' ? c - '0' : -1;
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	int max = pair == 0u ? 'R' : 'X';
	return c >= 'A' && c <= max ? c - 'A' : -1;
}

static const uint16_t mh_base[4] = { 18u, 10u, 24u, 10u };	// characters each pair can be

bool maidenhead_parse(const char *s, int32_t *lat_e7, int32_t *lon_e7)
{
	if (s == NULL)
		return false;
	while (*s == ' ')
		s++;
	size_t n = 0;
	while (s[n] != '\0' && s[n] != ' ')
		n++;
	for (size_t k = n; s[k] != '\0'; k++) {
		if (s[k] != ' ')
			return false;	// two words
	}
	if (n != 4u && n != 6u && n != 8u)
		return false;
	uint32_t lo = 0, la = 0, size = MH_CELLS;
	for (unsigned pair = 0; pair < 4u; pair++) {
		size /= mh_base[pair];
		int x = 0, y = 0;
		if (2u * pair < n) {
			x = mh_digit(s[2u * pair], pair);
			y = mh_digit(s[2u * pair + 1u], pair);
			if (x < 0 || y < 0)
				return false;
		} else {
			size *= mh_base[pair];	// not given: the square stays this big
			break;
		}
		lo += (uint32_t)x * size;
		la += (uint32_t)y * size;
	}
	// The center: (2 * index + size) half squares
	*lon_e7 = (int32_t)((uint64_t)(2u * lo + size) * 10000000u / 240u) - (int32_t)MH_LON_E7;
	*lat_e7 = (int32_t)((uint64_t)(2u * la + size) * 10000000u / 480u) - (int32_t)MH_LAT_E7;
	return true;
}

void maidenhead_format(int32_t lat_e7, int32_t lon_e7, uint8_t chars, char *out)
{
	if (chars != 4u && chars != 8u)
		chars = 6u;
	uint32_t lo = (uint32_t)lon_e7 + MH_LON_E7, la = (uint32_t)lat_e7 + MH_LAT_E7;	// unsigned: no overflow
	if (lo >= 2u * MH_LON_E7)
		lo = 2u * MH_LON_E7 - 1u;
	if (la >= 2u * MH_LAT_E7)
		la = 2u * MH_LAT_E7 - 1u;
	uint32_t x = (uint32_t)((uint64_t)lo * 120u / 10000000u), y = (uint32_t)((uint64_t)la * 240u / 10000000u);
	uint32_t size = MH_CELLS;
	for (unsigned pair = 0; 2u * pair < chars; pair++) {
		size /= mh_base[pair];
		char first = pair == 0u ? 'A' : pair == 2u ? 'a' : '0';
		out[2u * pair] = (char)(first + x / size % mh_base[pair]);
		out[2u * pair + 1u] = (char)(first + y / size % mh_base[pair]);
	}
	out[chars] = '\0';
}
