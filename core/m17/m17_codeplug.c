// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 channels in the codeplug (m17_codeplug.h, docs/spec/m17-codeplug.md)
#include <string.h>
#include "config.h"
#include "m17/m17_codeplug.h"
#include "codeplug/codeplug_modes.h"
#include "codeplug/codeplug_tag.h"
#include "codeplug/codeplug_records.h"

#if CONFIG_M17

// A destination's or a callsign's character as stored (upper case); '.' and space only inside a destination
static bool text_char(uint8_t c, bool dest)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || c == '-' || c == '/' || (dest && (c == '.' || c == ' '));
}

// A stored text field of n bytes: up to the first NUL, trailing spaces dropped, lower case read as upper (as the
// Codeplug Editor reads it); "" when any character is out of the set
static void text_get(char *out, const uint8_t *b, int len, uint8_t n, bool dest)
{
	uint8_t end = 0;

	while (end < n && (int)end < len && b[end] != 0)
		end++;
	while (end > 0 && b[end - 1] == ' ')
		end--;
	for (uint8_t i = 0; i < end; i++) {
		uint8_t c = b[i] >= 'a' && b[i] <= 'z' ? (uint8_t)(b[i] - 'a' + 'A') : b[i];

		if (!text_char(c, dest)) {
			out[0] = '\0';
			return;
		}
		out[i] = (char)c;
	}
	out[end] = '\0';
}

void m17_chan_decode(const uint8_t *b, int len, m17_chan_t *out)
{
	uint8_t b0 = len >= 1 ? b[0] : 0u;

	out->can = b0 & M17_CP_CAN_MASK;
	out->hear_any = (b0 & M17_CP_HEAR_ANY) != 0;
	out->dest[0] = '\0';
	if (len > 1)
		text_get(out->dest, b + 1, len - 1, M17_CP_DEST_LEN, true);
	if (strcmp(out->dest, "ALL") == 0)
		out->dest[0] = '\0';
}

void m17_chan_encode(const m17_chan_t *c, uint8_t out[M17_CP_CHAN_LEN])
{
	size_t n = strnlen(c->dest, M17_CP_DEST_LEN);

	out[0] = (uint8_t)((c->can & M17_CP_CAN_MASK) | (c->hear_any ? M17_CP_HEAR_ANY : 0u));
	memset(out + 1, ' ', M17_CP_DEST_LEN);
	memcpy(out + 1, c->dest, n);
}

void m17_radio_decode(const uint8_t *b, int len, m17_radio_t *out)
{
	out->callsign[0] = '\0';
	if (len > 0)
		text_get(out->callsign, b, len, M17_CP_CALL_LEN, false);
}

// The first word of a radio name as a callsign: 3 to 9 letters and digits with at least one of each (ysf.md 5.2's
// rule, nine characters for M17's address)
static void call_from_name(const char *name, char out[M17_CP_CALL_LEN + 1])
{
	uint8_t n = 0, letters = 0, digits = 0;

	out[0] = '\0';
	if (name == NULL)
		return;
	while (name[n] != '\0' && name[n] != ' ' && name[n] != '-' && name[n] != '/') {
		char c = name[n];

		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		if (n >= M17_CP_CALL_LEN || !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
			out[0] = '\0';
			return;
		}
		letters += c >= 'A';
		digits += c <= '9';
		out[n++] = c;
	}
	out[n] = '\0';
	if (n < 3 || !letters || !digits)
		out[0] = '\0';
}

void m17_callsign(const char *radio_name, char out[M17_CP_CALL_LEN + 1])
{
	uint8_t b[M17_CP_CALL_LEN + 8];
	m17_radio_t r;

	m17_radio_decode(b, codeplug_mode_settings(CODEPLUG_KIND_M17, b, sizeof b), &r);
	if (r.callsign[0] == '\0') {
		// The YSF record (read whatever CONFIG_YSF says): ten characters, which M17's nine may not hold
		uint8_t y[16];
		int len = codeplug_mode_settings(CODEPLUG_KIND_YSF, y, sizeof y);
		char ysf[11];

		ysf[0] = '\0';
		if (len > 0)
			text_get(ysf, y, len, 10, false);
		if (strlen(ysf) <= M17_CP_CALL_LEN)
			memcpy(r.callsign, ysf, strlen(ysf) + 1);
	}
	if (r.callsign[0] == '\0')
		call_from_name(radio_name, r.callsign);
	memcpy(out, r.callsign, sizeof r.callsign);
}

bool m17_chan_load(uint16_t number, const channel_t *ch, m17_chan_t *out)
{
	uint8_t b[M17_CP_CHAN_LEN + 8u + CODEPLUG_TAG_HEAD_MAX];
	const int cap = (int)(sizeof b - CODEPLUG_TAG_HEAD_MAX);
	int len;

	if (ch->kind != CODEPLUG_KIND_M17)
		return false;
	len = codeplug_tag_params(number, ch, b, (uint16_t)cap);
	if (len < 0)
		return false;
	m17_chan_decode(b, len < cap ? len : cap, out);
	return true;
}

#endif	// CONFIG_M17
