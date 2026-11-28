// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The M17 mode module of the Codeplug Editor (docs/spec/m17-codeplug.md): kind 7 on the channel-mode layer
// (channelmodes.js). Bytes to fields and back. The editor shows the kind and keeps its block; the fields the pages
// show and the radio-wide callsign record come with ADR 006 step 3.

import { u8, grow } from "./modebytes.js";

export const CAN_MAX = 15, DEST_LEN = 9;
const HEAR_ANY = 0x10, CAN_MASK = 0x0F;

// Byte 0: the CAN in bits 0-3, hear every CAN in bit 4, bits 5-7 reserved. Bytes 1-9: the destination, nine ASCII
// characters padded with spaces; blank or ALL is broadcast
export function decodeChan(b) {
	const b0 = u8(b, 0);
	let dest = "";
	for (let i = 1; i <= DEST_LEN && b && i < b.length && b[i] !== 0; i++) dest += String.fromCharCode(b[i]);
	dest = dest.trim().toUpperCase();
	return { can: b0 & CAN_MASK, hearAny: !!(b0 & HEAR_ANY), dest: dest === "ALL" ? "" : dest };
}

// The reserved bits of byte 0 stay as read, as grow keeps the bytes past the block
export function encodeChan(p, prev = null) {
	const v = grow(prev, 1 + DEST_LEN);
	const can = p.can >= 0 && p.can <= CAN_MAX ? p.can : 0;
	v[0] = (prev && prev.length ? prev[0] & ~(CAN_MASK | HEAR_ANY) & 0xFF : 0) | can | (p.hearAny ? HEAR_ANY : 0);
	const dest = String(p.dest ?? "").trim().toUpperCase();
	for (let i = 0; i < DEST_LEN; i++) v[1 + i] = i < dest.length ? dest.charCodeAt(i) : 0x20;
	return v;
}

export const m17Kind = {
	id: 7,
	feature: "M17",
	systemType: null,
	contacts: false,
	ownFreqs: true,
	defaultTx: true,
	recordFields: [],
	flags: {},
	newParams: () => ({ can: 0, hearAny: false, dest: "" }),
	decode: decodeChan,
	encode: encodeChan,
	fields: () => [],
	validate() {},
};
