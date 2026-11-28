// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// The codeplug editor's CSV tables (static/js/codeplug/csv.js): parsing, export and import round trips on cpsSim's
// populated codeplug, matching rows to existing records, references by name, partial files and problems.

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";

import { CodeplugImage, CODEPLUG_SPAN } from "../static/js/codeplug/image.js";
import { decode, encode, empty, newChannel, newContact } from "../static/js/codeplug/model.js";
import { channelAddr } from "../static/js/codeplug/records.js";
import { parse, stringify, detect, exportTable, importTable, exportAll, importAll, ORDER } from "../static/js/codeplug/csv.js";
import { setKind, newSystem } from "../static/js/codeplug/channelmodes.js";

const DIR = process.env.RFOUNDRY_SITE_FIXTURES || "build/site-tests";
const sim = () => decode(CodeplugImage.from(new Uint8Array(readFileSync(join(DIR, "codeplug_sim.bin"))).slice(0, CODEPLUG_SPAN)));

test("parse: quotes, doubled quotes, newlines in a field, CRLF, BOM, blank lines", () => {
	const rows = parse('﻿a,"b,c","say ""hi"""\r\n\r\n1,"two\nlines",\n');
	assert.deepEqual(rows, [["a", "b,c", 'say "hi"'], ["1", "two\nlines", ""]]);
});

test("parse: semicolon and tab files", () => {
	assert.deepEqual(parse("Name;RX\nA;438,25\n"), [["Name", "RX"], ["A", "438,25"]]);
	assert.deepEqual(parse("Name\tRX\nA\t438.25"), [["Name", "RX"], ["A", "438.25"]]);
});

test("stringify quotes what needs it and parses back", () => {
	const rows = [["a", "b,c", 'q"', " lead", "x\ny", ""]];
	assert.equal(stringify(rows), 'a,"b,c","q""",\" lead\","x\ny",\r\n');
	assert.deepEqual(parse(stringify(rows)), rows);
});

test("every table: export, import into the same codeplug, nothing changes", () => {
	const m = sim();
	const before = encode(m).bytes;
	for (const table of ORDER) {
		const text = exportTable(m, table);
		assert.equal(detect(text), table, table);
		const r = importTable(m, text);
		assert.deepEqual(r.problems, [], table);
		assert.equal(r.removed, 0, table);
		assert.equal(r.added, 0, table);
	}
	assert.deepEqual(encode(m).bytes, before);
});

test("all tables into a blank codeplug give the same records", () => {
	const m = sim();
	const files = exportAll(m).map((f) => ({ name: f.name, text: f.data }));
	const n = empty();
	const { results, skipped } = importAll(n, files);
	assert.deepEqual(skipped, []);
	for (const r of results) assert.deepEqual(r.problems, [], r.name);
	const strip = (x) => JSON.parse(JSON.stringify(x, (k, v) => (k === "base" || k === "slot" || k === "iddb" ? undefined : v)));
	// references compare by what they point at
	const view = (mm) => ({
		channels: mm.channels.map((c) => ({ ...c, contact: c.contact?.name ?? null, rxList: c.rxList?.name ?? null, aprs: c.aprs?.name ?? null })),
		zones: mm.zones.map((z) => ({ name: z.name, channels: z.channels.map((c) => c.number) })),
		contacts: mm.contacts, rxLists: mm.rxLists.map((l) => ({ ...l, contacts: l.contacts.map((c) => c.number) })),
		dtmfContacts: mm.dtmfContacts, aprs: mm.aprs,
	});
	assert.deepEqual(strip(view(n)), strip(view(m)));
	// and the blank codeplug's image decodes to the same again
	assert.deepEqual(strip(view(decode(encode(n)))), strip(view(m)));
});

test("rows match records by number, then name; a renamed contact keeps its channels", () => {
	const m = empty();
	const tg = newContact({ number: 1, name: "TG 91", id: 91 });
	const local = newContact({ number: 2, name: "Local", id: 9 });
	m.contacts.push(tg, local);
	const ch = newChannel({ number: 1, name: "Hotspot", mode: "dmr", contact: tg });
	m.channels.push(ch);
	const r = importTable(m, "Number,Name,ID,Call Type\n1,Worldwide,91,Group\n,Local,9,Group\n,Parrot,9990,Private\n");
	assert.deepEqual([r.added, r.updated, r.removed], [1, 2, 0]);
	assert.equal(m.contacts[0], tg);
	assert.equal(tg.name, "Worldwide");
	assert.equal(ch.contact, tg);
	assert.equal(m.contacts[1], local);
	assert.equal(m.contacts[2].number, 3);
	assert.equal(m.contacts[2].type, "private");
});

test("records missing from the file are removed and their references cleared", () => {
	const m = empty();
	const a = newContact({ number: 1, name: "A", id: 1 }), b = newContact({ number: 2, name: "B", id: 2 });
	m.contacts.push(a, b);
	m.rxLists.push({ number: 1, name: "L", contacts: [a, b] });
	const ch = newChannel({ number: 1, name: "C", mode: "dmr", contact: b });
	m.channels.push(ch);
	const r = importTable(m, "Name,ID\nA,1\n");
	assert.equal(r.removed, 1);
	assert.equal(ch.contact, null);
	assert.deepEqual(m.rxLists[0].contacts, [a]);
});

test("a bad value in an existing record keeps the old one", () => {
	const m = empty();
	const ch = newChannel({ number: 1, name: "Rpt", rxHz: 444000000 });
	m.channels.push(ch);
	const r = importTable(m, "Number,Name,RX Frequency\n1,Rpt,abc\n");
	assert.equal(m.channels[0], ch);
	assert.equal(ch.rxHz, 444000000);
	assert.deepEqual(r.problems.map((p) => [p.code, !!p.skipped]), [["bad_freq", false]]);
});

test("a partial file changes only its columns", () => {
	const m = empty();
	const ch = newChannel({ number: 1, name: "Rpt", rxHz: 444000000, txHz: 449000000, rxTone: "100.0", power: 9 });
	m.channels.push(ch);
	importTable(m, "Name,RX Frequency\nRpt,444.1\n", "channels");
	assert.equal(ch.rxHz, 444100000);
	assert.equal(ch.txHz, 449000000);
	assert.equal(ch.rxTone, "100.0");
	assert.equal(ch.power, 9);
});

test("references by name, '#n' for duplicate names, and problems for unknown ones", () => {
	const m = empty();
	const c1 = newChannel({ number: 1, name: "Same" }), c2 = newChannel({ number: 2, name: "Same" }), c3 = newChannel({ number: 3, name: "Other" });
	m.channels.push(c1, c2, c3);
	m.zones.push({ name: "Z", channels: [c2, c3], slot: 0 });
	const text = exportTable(m, "zones");
	assert.match(text, /"?#2\|Other"?/);
	const r = importTable(m, 'Name,Channels\nZ,#2|other|Nope|Same\n');
	assert.deepEqual(m.zones[0].channels, [c2, c3]);
	assert.deepEqual(r.problems.map((p) => p.code), ["unknown_ref", "ambiguous_ref"]);
});

test("a name with the list separator is written as its number", () => {
	const m = empty();
	const a = newChannel({ number: 7, name: "A|B" });
	m.channels.push(a);
	m.zones.push({ name: "Z", channels: [a], slot: 0 });
	assert.match(exportTable(m, "zones"), /^Z,#7\r$/m);
	assert.deepEqual(importTable(m, exportTable(m, "zones")).problems, []);
	assert.deepEqual(m.zones[0].channels, [a]);
});

test("values: aliases, Yes/No, power names, tones, bad values reported with row and column", () => {
	const m = empty();
	const text = [
		"Channel Name;RX Freq;TX Freq;Mode;TX Power;Decode;Encode;CC;TS;RX Only;Bandwidth",
		"Simplex;446,00625;446,00625;fm;high;88,5;d23;;;yes;25",
		"Bad;abc;438.25;FM;Huge;12;D023N;;;maybe;20",
		";438.25;438.25;FM;;;;;;;",
	].join("\n");
	assert.equal(detect(text), "channels");
	const r = importTable(m, text);
	const [a] = m.channels;
	assert.equal(m.channels.length, 1);
	assert.deepEqual([a.rxHz, a.mode, a.power, a.rxTone, a.txTone, a.rxOnly, a.bw], [446006250, "fm", 9, "88.5", "D023N", true, 25]);
	// row 3 is new and its RX frequency is bad: reported and skipped; row 4 has no name
	assert.deepEqual(r.problems.map((p) => [p.row, p.column, p.code, !!p.skipped]), [
		[3, "RX Frequency (MHz)", "bad_freq", true], [3, "Power", "bad_value", false], [3, "RX Tone", "bad_tone", false],
		[3, "RX Only", "bad_bool", false], [3, "Bandwidth (kHz)", "bad_value", false], [4, "Name", "no_name", true],
	]);
	assert.deepEqual(r.unknown, []);
});

test("unknown headers are listed, and a file of no table is refused", () => {
	const m = empty();
	const r = importTable(m, "Name,ID,Favorite Color\nA,1,blue\n");
	assert.equal(r.table, "contacts");
	assert.deepEqual(r.unknown, ["Favorite Color"]);
	assert.equal(detect("Foo,Bar\n1,2\n"), null);
	assert.throws(() => importTable(m, "Foo,Bar\n1,2\n"), { code: "not_a_table" });
});

test("a CSV TX timeout rounds up to the 15 s steps the record stores (#924)", () => {
	const m = empty();
	const r = importTable(m, "Name,RX Frequency (MHz),TX Frequency (MHz),Timeout (s)\nA,446.0,446.0,100\nB,446.0,446.0,3825\nC,446.0,446.0,1\nD,446.0,446.0,\n", "channels");
	assert.deepEqual(r.problems, []);
	assert.deepEqual(m.channels.map((c) => c.tot), [105, 3825, 15, 0]);
	// 100 s is written as 7 units of 15 s, and reads back as the 105 s the editor shows
	const img = encode(m);
	assert.equal(img.bytes[channelAddr(1) + 0x1B], 7);
	assert.deepEqual(decode(img).channels.map((c) => c.tot), [105, 3825, 15, 0]);
});

// --- Channel modes (P25, NXDN, YSF and the trunked kinds) ---

// A codeplug with a channel of each kind a file can make, and a trunked one
function kinds() {
	const m = empty();
	const add = (name, kind, hz, params = null) => {
		const c = newChannel({ name, rxHz: hz, txHz: hz });
		m.channels.push(c);
		if (kind) setKind(m, c, kind);
		if (params) Object.assign(c.params, params);
		return c;
	};
	add("FM", 0, 146520000);
	add("YSF VW", 103, 145587500, { txDgid: 12, rxDgid: 0, voice: "vw", narrow: true });
	add("YSF VD1", 103, 438500000, { txDgid: 0, rxDgid: 40, voice: "vd1", narrow: false });
	add("P25", 2, 441000000);
	add("NXDN", 4, 441100000);
	m.systems.push(newSystem(m, 1, "Site"));
	add("T3", 1, 851012500);
	m.channels.forEach((c, i) => { c.number = i + 1; });
	m.channels[2].tagTx = false;	// receive only, as the channel form sets it
	return m;
}
const row = (text, name) => {
	const rows = parse(text);
	const r = rows.find((x) => x[rows[0].indexOf("Name")] === name);
	return Object.fromEntries(rows[0].map((h, i) => [h, r[i]]));
};

test("a channel's mode is its kind in a file, with its YSF fields, and RX Only as the channel form shows it", () => {
	const text = exportTable(kinds(), "channels");
	assert.deepEqual(["FM", "YSF VW", "YSF VD1", "P25", "NXDN", "T3"].map((n) => row(text, n).Mode), ["FM", "YSF", "YSF", "P25", "NXDN", "T3"]);
	const vw = row(text, "YSF VW");
	assert.deepEqual([vw["YSF TX DG-ID"], vw["YSF RX DG-ID"], vw["YSF Voice Mode"], vw["YSF Deviation"], vw["RX Only"]], ["12", "0", "VW", "Narrow", "No"]);
	const vd1 = row(text, "YSF VD1");
	assert.deepEqual([vd1["YSF Voice Mode"], vd1["YSF Deviation"], vd1["RX Only"]], ["V/D 1", "Wide", "Yes"]);
	assert.equal(row(text, "FM")["YSF Voice Mode"], "", "other channels leave the YSF columns blank");
});

test("channels of every kind read back from their file unchanged, into the same codeplug and into a blank one", () => {
	const m = kinds();
	const text = exportTable(m, "channels");
	const view = (x) => x.channels.map((c) => [c.name, c.kind || 0, c.mode, c.kind ? !c.tagTx : c.rxOnly, c.kind === 103 ? { ...c.params } : null]);
	const before = view(m);
	const same = importTable(m, text);
	assert.deepEqual(same.problems, []);
	assert.deepEqual(view(m), before);
	// a blank codeplug has no system for the trunked channel: it is left out, with its reason; the rest arrive whole
	const blank = empty();
	const r = importTable(blank, text);
	assert.deepEqual(r.problems.map((p) => [p.row, p.column, p.code]), [[7, "Mode", "mode_system"]]);
	assert.deepEqual(view(blank).filter(([n]) => n !== "T3"), before.filter(([n]) => n !== "T3"));
	// and the image holds them: encoded and decoded, the YSF block is the editor's
	const back = decode(encode(blank));
	assert.deepEqual(back.channels.filter((c) => c.kind === 103).map((c) => c.params),
		[{ txDgid: 12, rxDgid: 0, voice: "vw", narrow: true }, { txDgid: 0, rxDgid: 40, voice: "vd1", narrow: false }]);
});

test("a YSF file from another tool: its column names, C4FM, the YSF fields before Mode", () => {
	const m = empty();
	const r = importTable(m, "Channel Name,YSF DT,Deviation,DG-ID,RX DG-ID,Channel Mode,RX Frequency,TX Frequency\n" +
		"Room,VW,Narrow,22,22,C4FM,439.5,439.5\nLocal,2,Wide,0,0,C4FM,145.6,145.6\nOld,V/D mode 1,W,,,YSF,438.1,438.1\n");
	assert.deepEqual(r.problems, []);
	assert.deepEqual(m.channels.map((c) => [c.name, c.kind, c.params]), [
		["Room", 103, { txDgid: 22, rxDgid: 22, voice: "vw", narrow: true }],
		["Local", 103, { txDgid: 0, rxDgid: 0, voice: "dn", narrow: false }],
		["Old", 103, { txDgid: 0, rxDgid: 0, voice: "vd1", narrow: false }],
	]);
	assert.ok(m.channels.every((c) => c.tagTx), "a channel from a file transmits unless RX Only says otherwise");
});

test("YSF values a channel can't take are problems naming row and column; the row is kept", () => {
	const m = empty();
	const r = importTable(m, "Name,Mode,RX Frequency,YSF Voice Mode,YSF Deviation,YSF TX DG-ID\n" +
		"A,FM,146.52,VW,,\nB,YSF,145.6,Turbo,Medium,100\n");
	assert.deepEqual(r.problems.map((p) => [p.row, p.column, p.code, !!p.skipped]), [
		[2, "YSF Voice Mode", "not_ysf", false],
		[3, "YSF Voice Mode", "bad_value", false],
		[3, "YSF Deviation", "bad_value", false],
		[3, "YSF TX DG-ID", "range", false],
	]);
	assert.deepEqual(m.channels.map((c) => [c.name, c.kind || 0]), [["A", 0], ["B", 103]]);
	assert.deepEqual(m.channels[1].params, { txDgid: 0, rxDgid: 0, voice: "dn", narrow: false }, "bad cells leave the defaults");
	// a channel made plain by its file drops its kind; a bad Mode word lists what a file can make
	const k = kinds();
	const plain = importTable(k, "Number,Name,Mode\n2,YSF VW,FM\n", "channels");
	assert.deepEqual(plain.problems, []);
	assert.equal(k.channels.find((c) => c.name === "YSF VW").kind, 0);
	const bad = importTable(empty(), "Name,Mode,RX Frequency\nX,D-Star,145.6\n").problems[0];
	assert.equal(bad.code, "bad_value");
	assert.match(bad.params.allowed, /^FM, DMR, P25, NXDN, YSF/);
});
