// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// Digital AGC policy (radio/digital_agc.h): the level each mode uses with a switch On or Off, the stored P25 and NXDN
// bytes, and every board's defaults (boards/*/board.c) being a value of the lists
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "radio/digital_agc.h"

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void test_policy(void)
{
	// The defaults: -30 is RX choice 3 and TX choice 1, -25 RX 5, -26 TX 3, -20 TX 6
	CHECK(dagc_rx(false, 9, 0) == 3);
	CHECK(dagc_rx(false, 9, -30) == 3);
	CHECK(dagc_rx(false, 9, -25) == 5);
	CHECK(dagc_rx(false, 9, -29) == 3);	// not in the list: the core default
	CHECK(dagc_rx(true, 9, -25) == 9);	// manual: the user's row
	CHECK(dagc_rx(true, 0, -25) == 0);	// manual Off stays Off
	CHECK(dagc_tx(DAGC_TX_DMR, false, 0, 0) == 1);
	CHECK(dagc_tx(DAGC_TX_DMR, false, 0, -26) == 3);
	CHECK(dagc_tx(DAGC_TX_NXDN, false, 0, -26) == 3 && dagc_tx(DAGC_TX_YSF, false, 0, -26) == 3);
	CHECK(dagc_tx(DAGC_TX_P25, false, 0, -26) == 6);	// P25 is -20 on every radio
	CHECK(TX_LEVEL_CHOICE_DBFS(6) == -20);
	CHECK(dagc_tx(DAGC_TX_DMR, false, 0, -27) == 1);	// not in the list
	CHECK(dagc_tx(DAGC_TX_P25, true, 0, -26) == 0 && dagc_tx(DAGC_TX_YSF, true, 4, -26) == 4);
	// Stored bytes: a choice + 1, 0 = unset
	CHECK(dagc_tx_p25_stored(0) == 6 && dagc_tx_p25_stored(1) == 0 && dagc_tx_p25_stored(4) == 3);
	CHECK(dagc_tx_p25_stored(200) == 6);
	CHECK(dagc_tx_nxdn_stored(0, 5) == 5 && dagc_tx_nxdn_stored(3, 5) == 2 && dagc_tx_nxdn_stored(0, 99) == 0);
}

// Each board's defaults, read from its board.c, map to exactly that target (not the core default's fallback)
static void test_boards(void)
{
	DIR *d = opendir(BOARDS_DIR);
	CHECK(d != NULL);
	if (d == NULL)
		return;
	struct dirent *e;
	unsigned boards = 0;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.')
			continue;
		char path[512];
		snprintf(path, sizeof path, "%s/%s/board.c", BOARDS_DIR, e->d_name);
		FILE *f = fopen(path, "r");
		if (f == NULL)
			continue;
		char line[512];
		while (fgets(line, sizeof line, f) != NULL) {
			const char *p;
			int db;
			if ((p = strstr(line, ".rx_agc_default_db =")) != NULL && sscanf(p, ".rx_agc_default_db = %d", &db) == 1) {
				boards++;
				if (rx_agc_target(dagc_rx_choice_of_db(db)) != db) {
					printf("FAIL %s: rx_agc_default_db %d is not an RX AGC target\n", path, db);
					failures++;
				}
			}
			if ((p = strstr(line, ".tx_level_default_db =")) != NULL &&
			    sscanf(p, ".tx_level_default_db = %d", &db) == 1 &&
			    TX_LEVEL_CHOICE_DBFS(dagc_tx_choice_of_db(db)) != db) {
				printf("FAIL %s: tx_level_default_db %d is not a TX level choice\n", path, db);
				failures++;
			}
		}
		fclose(f);
	}
	closedir(d);
	CHECK(boards > 0);
}

int main(void)
{
	test_policy();
	test_boards();
	printf("digitalAgcTest: %u checks, %u failed\n", checks, failures);
	return failures ? 1 : 0;
}
