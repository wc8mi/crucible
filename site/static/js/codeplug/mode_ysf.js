// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// The YSF (C4FM) mode module of the Codeplug Editor (docs/spec/ysf-codeplug.md): kind 103 (0x67) on the
// channel-mode layer (channelmodes.js). Bytes to fields and back, and the fields the pages show, as data. YSF names
// its caller and us by callsign: no contacts. The radio ID is never in the codeplug: every radio makes its own from
// its chip (docs/spec/ysf.md 6), so this module has no field for it.

import { u8, grow } from "./modebytes.js";

export const DGID_MAX = 99, CALL_LEN = 10;
const CALL_RE = /^[0-9A-Z/-]{0,10}$/;

// --- Channel (2) ---

// Byte 2 (ADR 005 6): the transmit data type in bits 0-1 (the FICH DT code), Narrow in bit 2, bits 3-7 reserved.
// A block without it is DN Wide. VOICES: the voice modes by their data type code; 1 (data) reads as DN
export const VOICES = ["dn", "vw", "vd1"];
const DT = { vd1: 0, dn: 2, vw: 3 };
const NARROW = 0x04, RESERVED = 0xF8;

export function decodeChan(b) {
	const tx = u8(b, 0), rx = u8(b, 1);
	const b2 = b && b.length >= 3 ? b[2] : DT.dn;
	const voice = { 0: "vd1", 3: "vw" }[b2 & 3] ?? "dn";
	return { txDgid: tx <= DGID_MAX ? tx : 0, rxDgid: rx <= DGID_MAX ? rx : 0, voice, narrow: !!(b2 & NARROW) };
}

// The reserved bits of byte 2 stay as read, as grow keeps the bytes past the block
export function encodeChan(p, prev = null) {
	const v = grow(prev, 3);
	v[0] = p.txDgid >= 0 && p.txDgid <= DGID_MAX ? p.txDgid : 0;
	v[1] = p.rxDgid >= 0 && p.rxDgid <= DGID_MAX ? p.rxDgid : 0;
	v[2] = (prev && prev.length >= 3 ? prev[2] & RESERVED : 0) | (DT[p.voice] ?? DT.dn) | (p.narrow ? NARROW : 0);
	return v;
}

// --- The radio-wide record (1): the callsign, "" = the radio name's ---

export function decodeRadio(b) {
	let s = "";
	for (let i = 0; i < CALL_LEN && b && i < b.length && b[i] !== 0; i++) s += String.fromCharCode(b[i]);
	s = s.replace(/ +$/, "");
	return { callsign: CALL_RE.test(s) ? s : "" };
}

export function encodeRadio(s, prev = null) {
	const call = String(s.callsign ?? "").trim().toUpperCase();
	if (!call) return null;
	const v = grow(prev, CALL_LEN);
	v.fill(0, 0, CALL_LEN);
	for (let i = 0; i < call.length && i < CALL_LEN; i++) v[i] = call.charCodeAt(i);
	return v;
}

const dgidField = (key, label, extra) => ({ key, label, type: "int", min: 0, max: DGID_MAX, ...extra });

export const ysfKind = {
	id: 103,
	feature: "YSF",
	systemType: null,
	contacts: false,
	ownFreqs: true,
	defaultTx: true,
	recordFields: [],
	flags: {},
	newParams: () => ({ txDgid: 0, rxDgid: 0, voice: "dn", narrow: false }),
	decode: decodeChan,
	encode: encodeChan,
	fields: () => [
		dgidField("txDgid", "editor.ysf.tx_dgid"),
		dgidField("rxDgid", "editor.ysf.rx_dgid", { help: "editor.ysf.dgid_any" }),
		{ key: "voice", label: "editor.ysf.voice", type: "select", help: "editor.ysf.voice_help",
			options: () => VOICES.map((v) => [v, `editor.ysf.voice_${v}`]) },
		{ key: "narrow", label: "editor.ysf.deviation", type: "select", help: "editor.ysf.deviation_help",
			options: () => [[false, "editor.ysf.wide"], [true, "editor.ysf.narrow"]] },
	],
	validate() {},
	settings: {
		key: "ysf",
		decode: decodeRadio,
		encode: encodeRadio,
		fields: [{ key: "callsign", label: "editor.ysf.callsign", type: "text", help: "editor.ysf.callsign_help" }],
		validate(s, problem) {
			const call = String(s?.callsign ?? "").trim().toUpperCase();
			if (call && !CALL_RE.test(call)) problem.err("callsign", "ysf_callsign");
		},
	},
};
