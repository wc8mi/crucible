// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 channels in the codeplug (docs/spec/m17-codeplug.md): kind 7's parameter block and the radio-wide record, from
// the stored bytes to plain values the radio core sets the M17 engine up from. Bytes in, values out: no engine here.
// Built with CONFIG_M17
#ifndef RFOUNDRY_M17_CODEPLUG_H
#define RFOUNDRY_M17_CODEPLUG_H

#include <stdbool.h>
#include <stdint.h>
#include "codeplug/codeplug.h"

#define M17_CP_CAN_MAX		15u
#define M17_CP_DEST_LEN		9u	// the destination's characters, space padded
#define M17_CP_CALL_LEN		9u	// the radio-wide record's callsign field
#define M17_CP_CHAN_LEN		(1u + M17_CP_DEST_LEN)

// Byte 0
#define M17_CP_CAN_MASK		0x0Fu
#define M17_CP_HEAR_ANY		0x10u	// Crucible's own: hear every CAN

typedef struct {
	uint8_t can;				// 0-15
	bool hear_any;				// play every CAN, else only can
	char dest[M17_CP_DEST_LEN + 1];		// "" = BROADCAST ("ALL" reads as "")
} m17_chan_t;

// Decode len bytes of a block (a short one, or none, takes the defaults: CAN 0, BROADCAST); a destination with a
// character outside the set reads as BROADCAST; byte 0's reserved bits are ignored
void m17_chan_decode(const uint8_t *b, int len, m17_chan_t *out);
void m17_chan_encode(const m17_chan_t *c, uint8_t out[M17_CP_CHAN_LEN]);

// The radio-wide record (key 0x0A07): the source callsign, "" where unset or not valid
typedef struct {
	char callsign[M17_CP_CALL_LEN + 1];
} m17_radio_t;
void m17_radio_decode(const uint8_t *b, int len, m17_radio_t *out);
// The callsign we send as: the record's, else the YSF record's, else the radio name's (m17-codeplug.md 3). "" none
void m17_callsign(const char *radio_name, char out[M17_CP_CALL_LEN + 1]);

// Channel number's block from its tag (ch read by codeplug_channel); false when it isn't a live M17 channel
bool m17_chan_load(uint16_t number, const channel_t *ch, m17_chan_t *out);

#endif
