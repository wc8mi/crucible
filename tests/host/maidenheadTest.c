// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// Maidenhead locators (core/position/maidenhead.c): parse to the square's center, format, and back
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "position/maidenhead.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// Within 1e-7 degree of the expected (the halves of a square are not always whole 1e-7 degree)
static bool near(int32_t a, int32_t b) { return abs(a - b) <= 1; }

static void test_parse(void)
{
	int32_t lat, lon;
	CHECK(maidenhead_parse("EN82", &lat, &lon) && lat == 425000000 && lon == -830000000);
	// EN82mn: 42.5625 N, 82.9583 W (5' x 2.5' subsquare, its center)
	CHECK(maidenhead_parse("EN82mn", &lat, &lon) && near(lat, 425625000) && near(lon, -829583333));
	CHECK(maidenhead_parse("en82MN", &lat, &lon) && near(lat, 425625000) && near(lon, -829583333));
	CHECK(maidenhead_parse("  EN82mn ", &lat, &lon) && near(lat, 425625000));
	// FN31pr47 (an eight-character square: 30" x 15")
	CHECK(maidenhead_parse("FN31pr47", &lat, &lon) && near(lat, 417395833) && near(lon, -727125000));
	CHECK(maidenhead_parse("AA00", &lat, &lon) && lat == -895000000 && lon == -1790000000);
	CHECK(maidenhead_parse("RR99xx99", &lat, &lon) && near(lat, 899979167) && near(lon, 1799958333));
	CHECK(maidenhead_parse("JJ00aa", &lat, &lon) && lat != 0 && lon != 0);	// never the "not set" 0, 0

	CHECK(!maidenhead_parse("", &lat, &lon));
	CHECK(!maidenhead_parse("EN8", &lat, &lon));
	CHECK(!maidenhead_parse("EN82m", &lat, &lon));
	CHECK(!maidenhead_parse("EN82mn4", &lat, &lon));
	CHECK(!maidenhead_parse("EN82mn47a", &lat, &lon));
	CHECK(!maidenhead_parse("SN82", &lat, &lon));	// fields stop at R
	CHECK(!maidenhead_parse("ENA2", &lat, &lon));
	CHECK(!maidenhead_parse("EN82yz", &lat, &lon));	// subsquares stop at X
	CHECK(!maidenhead_parse("EN82 mn", &lat, &lon));
	CHECK(!maidenhead_parse(NULL, &lat, &lon));
}

static void test_format(void)
{
	char out[MAIDENHEAD_MAX + 1];
	maidenhead_format(425625000, -829583333, 6, out);
	CHECK(strcmp(out, "EN82mn") == 0);
	maidenhead_format(425625000, -829583333, 4, out);
	CHECK(strcmp(out, "EN82") == 0);
	maidenhead_format(417395833, -727125000, 8, out);
	CHECK(strcmp(out, "FN31pr47") == 0);
	maidenhead_format(423600000, -710600000, 6, out);	// Boston
	CHECK(strcmp(out, "FN42li") == 0);
	maidenhead_format(900000000, 1800000000, 6, out);	// the corner is held in the last square
	CHECK(strcmp(out, "RR99xx") == 0);
	maidenhead_format(-900000000, -1800000000, 8, out);
	CHECK(strcmp(out, "AA00aa00") == 0);
	maidenhead_format(0, 0, 3, out);			// not 4 or 8: 6
	CHECK(strcmp(out, "JJ00aa") == 0);
}

// Every center formats back to its own locator
static void test_round_trip(void)
{
	char in[MAIDENHEAD_MAX + 1], out[MAIDENHEAD_MAX + 1];
	int32_t lat, lon;
	unsigned bad = 0;
	for (int f = 0; f < 18; f += 3)
		for (int g = 0; g < 18; g += 2)
			for (int s = 0; s < 24; s += 5)
				for (int e = 0; e < 10; e += 3) {
					snprintf(in, sizeof in, "%c%c%d%d%c%c%d%d", 'A' + f, 'A' + g, e, 9 - e, 'a' + s,
						 'a' + 23 - s, e, 9 - e);
					if (!maidenhead_parse(in, &lat, &lon)) {
						bad++;
						continue;
					}
					maidenhead_format(lat, lon, 8, out);
					bad += strcmp(in, out) != 0;
				}
	CHECK(bad == 0);
}

int main(void)
{
	test_parse();
	test_format();
	test_round_trip();
	printf("# maidenheadTest: %d checks, %s\n", checks, failures ? "FAILED" : "PASSED");
	return failures != 0;
}
