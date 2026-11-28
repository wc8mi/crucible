// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 in the codeplug (core/m17/m17_codeplug.c, docs/spec/m17-codeplug.md) on a simulated NOR flash: the channel
// block (CAN, hear every CAN, destination), a channel's block from its tag, and the callsign: the M17 record, the YSF
// record, the radio name
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codeplug/codeplug.h"
#include "codeplug/codeplug_kv.h"
#include "codeplug/codeplug_tag.h"
#include "codeplug/codeplug_records.h"
#include "m17/m17_codeplug.h"
#include "storage/storage.h"
#include "sim/sim_flash.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

void storage_lock(void) {}
void storage_unlock(void) {}

// The board's air interface (core/radio/radio_m17.c in the firmware): whether the radio runs kind 7 here
static bool fits = true;
bool radio_m17_fits(void) { return fits; }

#define FLASH_SIZE	0x200000u

static const hal_flash_map_t map = {
	.settings_addr = 0x11000u, .settings_size = 0x8000u, .journal_addr = 0x19000u,
	.openfw_eeprom = 0, .openfw_flash = 0x20000u, .openfw_end = FLASH_SIZE,
	.calibration_addr = 0x10000u, .calibration_size = 0x200u,
};

static band_t bands[] = { { 136000000, 174000000, true }, { 400000000, 900000000, true } };

const board_t board = { .name = "test", .bands = bands, .band_count = 2, .power_count = 9, .flash = &sim_flash };

static void fresh(void)
{
	sim_flash_create(FLASH_SIZE, &map);
	CHECK(storage_init());
	codeplug_init();
}

static channel_t make(const char *name, uint32_t rx, uint32_t tx)
{
	channel_t c;

	memset(&c, 0, sizeof c);
	strncpy(c.name, name, CODEPLUG_NAME_LEN);
	c.rx_hz = rx;
	c.tx_hz = tx;
	c.mode = RADIO_MODE_DMR;
	c.rx_only = true;
	c.power = CODEPLUG_POWER_DEFAULT;
	c.color_code = 1;
	c.timeslot = 1;
	c.system = CODEPLUG_SYSTEM_NONE;
	return c;
}

static bool dec(const void *b, int len, uint8_t can, bool any, const char *dest)
{
	m17_chan_t c;

	memset(&c, 0x5A, sizeof c);
	m17_chan_decode((const uint8_t *)b, len, &c);
	return c.can == can && c.hear_any == any && strcmp(c.dest, dest) == 0;
}

static void test_block(void)
{
	uint8_t out[M17_CP_CHAN_LEN];
	m17_chan_t c = { .can = 9, .hear_any = true, .dest[0] = 0 }, back;

	// Defaults: no block, a short one
	CHECK(dec(NULL, 0, 0, false, ""));
	CHECK(dec("\x05", 1, 5, false, ""));
	CHECK(dec("\x15", 1, 5, true, ""));
	// Reserved bits ignored
	CHECK(dec("\xE3" "W1AW     ", 10, 3, false, "W1AW"));
	// The destination: NUL or space padded, "ALL" is BROADCAST, a reflector module with its space and letter
	CHECK(dec("\x00" "W1AW\0\0\0\0\0", 10, 0, false, "W1AW"));
	CHECK(dec("\x00" "ALL      ", 10, 0, false, ""));
	CHECK(dec("\x00" "M17-M17 C", 10, 0, false, "M17-M17 C"));
	CHECK(dec("\x00" "#ECHO    ", 10, 0, false, ""));	// '#': out of the set
	CHECK(dec("\x00" "w1aw     ", 10, 0, false, "W1AW"));	// lower case reads as upper
	CHECK(dec("\x02" "N0CALL", 7, 2, false, "N0CALL"));	// a block shorter than 10
	// Bytes past the block are not read
	CHECK(dec("\x01" "AB1CD    XYZ", 13, 1, false, "AB1CD"));
	// Round trip
	m17_chan_encode(&c, out);
	CHECK(out[0] == 0x19 && memcmp(out + 1, "         ", 9) == 0);
	m17_chan_decode(out, sizeof out, &back);
	CHECK(back.can == 9 && back.hear_any && back.dest[0] == '\0');
	c.can = 15;
	c.hear_any = false;
	strcpy(c.dest, "M17-M17 C");
	m17_chan_encode(&c, out);
	CHECK(out[0] == 0x0F && memcmp(out + 1, "M17-M17 C", 9) == 0);
	c.can = 31;	// out of range: masked
	m17_chan_encode(&c, out);
	CHECK(out[0] == 0x0F);
}

static void test_load(void)
{
	m17_chan_t mc = { .can = 7, .hear_any = false }, back;
	uint8_t blk[M17_CP_CHAN_LEN];
	codeplug_tag_t t = { .kind = CODEPLUG_KIND_M17, .system = CODEPLUG_SYSTEM_NONE, .flags = CODEPLUG_TAG_TX };
	channel_t c;

	fresh();
	strcpy(mc.dest, "W1AW");
	c = make("M17", 438900000, 438900000);
	CHECK(codeplug_set_channel(10, &c));
	m17_chan_encode(&mc, blk);
	CHECK(codeplug_tag_write(10, "M17", &t, blk, sizeof blk));
	CHECK(codeplug_channel(10, &c) && c.kind == CODEPLUG_KIND_M17 && !c.rx_only);
	fits = false;
	CHECK(!codeplug_kind_built(CODEPLUG_KIND_M17) && strcmp(codeplug_kind_name(c.kind), "M17") == 0);
	fits = true;
	CHECK(codeplug_kind_built(CODEPLUG_KIND_M17));
	CHECK(m17_chan_load(10, &c, &back) && back.can == 7 && !back.hear_any && strcmp(back.dest, "W1AW") == 0);
	// No block: CAN 0, BROADCAST; receive only with the tag's flag clear
	c = make("Bare", 438925000, 438925000);
	CHECK(codeplug_set_channel(11, &c));
	t.flags = 0;
	CHECK(codeplug_tag_write(11, "Bare", &t, NULL, 0));
	CHECK(codeplug_channel(11, &c) && c.kind == CODEPLUG_KIND_M17 && c.rx_only);
	CHECK(m17_chan_load(11, &c, &back) && back.can == 0 && !back.hear_any && back.dest[0] == '\0');
	// A plain channel is not M17
	channel_t plain = make("Plain", 438950000, 438950000);
	CHECK(codeplug_set_channel(12, &plain) && codeplug_channel(12, &plain) && !m17_chan_load(12, &plain, &back));
}

static void set_rec(uint8_t kind, const char *s, uint16_t n)
{
	CHECK(codeplug_kv_set((uint16_t)(CODEPLUG_KEY_MODE_SET + kind), (const uint8_t *)s, n));
}

static void test_callsign(void)
{
	char out[M17_CP_CALL_LEN + 1];
	m17_radio_t r;

	fresh();
	// Nothing stored: the radio name's first word, 3-9 letters and digits with both
	m17_callsign("n0call Jamie", out);
	CHECK(strcmp(out, "N0CALL") == 0);
	m17_callsign("Base", out);
	CHECK(out[0] == '\0');
	m17_callsign("AB1CDEFGH", out);
	CHECK(strcmp(out, "AB1CDEFGH") == 0);
	m17_callsign("AB1CDEFGHI", out);	// ten: too long for M17
	CHECK(out[0] == '\0');
	m17_callsign(NULL, out);
	CHECK(out[0] == '\0');
	// The YSF record when it fits in nine
	set_rec(CODEPLUG_KIND_YSF, "KF8S\0\0\0\0\0\0", 10);
	m17_callsign("N0CALL", out);
	CHECK(strcmp(out, "KF8S") == 0);
	set_rec(CODEPLUG_KIND_YSF, "AB1CDEFGHI", 10);
	m17_callsign("N0CALL", out);
	CHECK(strcmp(out, "N0CALL") == 0);
	// The M17 record first
	set_rec(CODEPLUG_KIND_M17, "W1AW/P   ", 9);
	m17_callsign("N0CALL", out);
	CHECK(strcmp(out, "W1AW/P") == 0);
	// A record with a character out of the set is unset
	m17_radio_decode((const uint8_t *)"W1 AW", 5, &r);
	CHECK(r.callsign[0] == '\0');
	m17_radio_decode((const uint8_t *)"w1aw", 4, &r);
	CHECK(strcmp(r.callsign, "W1AW") == 0);
	m17_chan_t c;
	m17_chan_decode((const uint8_t *)"\x00" "all", 4, &c);
	CHECK(c.dest[0] == '\0');
	m17_radio_decode(NULL, 0, &r);
	CHECK(r.callsign[0] == '\0');
}

int main(void)
{
	test_block();
	test_load();
	test_callsign();
	printf("# m17CodeplugTest %s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
	return failures != 0;
}
