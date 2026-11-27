// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Behnke
// One codeplug for several radios (static/js/codeplug/targets.js): the radios.json profiles become targets, a power
// byte reads as each radio's own level, and a channel a radio can't use is a warning naming that radio.
import { test } from "node:test";
import assert from "node:assert/strict";
import { radioTargets, targetFor, powerOn, powerAcross, channelOn, targetProblems } from "../static/js/codeplug/targets.js";
import { powerByte } from "../static/js/codeplug/power.js";
import { empty, newChannel } from "../static/js/codeplug/model.js";
import { channelForm, powerOptions } from "../static/js/editor/views.js";

const RATED = [[136000000, 174000000, true], [400000000, 480000000, true]];
const ALL = ["DMR", "P25", "NXDN", "YSF", "DMR_AES"];
// the shapes tools/boards.py radio_profiles() writes, for the radios that differ most
const radios = [
	{ id: "five", name: "Five Level", config: { full: ALL }, codeplug: { profiles: [
		{ board_name: "FIVE", build: "", title: "", names: ["100mW", "500mW", "1W", "2.5W", "5W"], bands: [], default: 2, ranges: RATED },
	] } },
	{ id: "mobile", name: "Mobile", config: { full: ALL }, codeplug: { profiles: [
		{ board_name: "MOBILE", build: "", title: "", names: ["100mW", "1W", "5W", "25W", "45W"], default: 2, ranges: RATED,
			bands: [{ lo: 136000000, hi: 174000000, names: ["250mW", "1W", "5W", "25W", "50W"] }] },
	] } },
	{ id: "nine", name: "Nine Level", config: { full: ["DMR"] }, codeplug: { profiles: [
		{ board_name: "NINE", build: "5w", title: "5 W", names: ["50mW", "250mW", "500mW", "750mW", "1W", "2W", "3W", "4W", "5W"], bands: [], default: 4,
			ranges: [...RATED, [108000000, 136000000, false], [200000000, 260000000, false, true]] },
		{ board_name: "NINE 10W", build: "10w", title: "10 W", names: ["50mW", "250mW", "500mW", "750mW", "1W", "2W", "3W", "5W", "10W"], bands: [], default: 4, ranges: RATED },
	] } },
	{ id: "old", name: "No Profile", codeplug: { names: ["OLD"] } },
];
const targets = radioTargets(radios);
const t = (k, p = {}) => k + (Object.keys(p).length ? JSON.stringify(p) : "");

test("each image of each radio is a target; a radio without a profile is none", () => {
	assert.deepEqual(targets.map((x) => x.key), ["five", "mobile", "nine", "nine:10w"]);
	assert.deepEqual(targets.map((x) => x.name), ["Five Level", "Mobile", "Nine Level 5 W", "Nine Level 10 W"]);
	assert.equal(targetFor(targets, "nine 10w").key, "nine:10w");
	assert.equal(targetFor(targets, "OLD"), null);
	// older firmware reports an alias: the radio's first image
	assert.equal(targetFor(targets, "NINE-ALIAS", "nine").key, "nine");
});

test("a power byte is the same relative level on every radio, in the channel's TX band", () => {
	const top = powerAcross(targets, 10, 145000000).map((x) => x.label);
	assert.deepEqual(top, ["5W", "50W", "5W", "10W"]);
	assert.deepEqual(powerAcross(targets, 1, 435000000).map((x) => x.label), ["100mW", "100mW", "50mW", "50mW"]);
	// a five-level radio's levels land on the nine-level radio's scale in order, and back
	const five = targets[0], nine = targets[2];
	const there = [0, 1, 2, 3, 4].map((level) => powerOn(nine, powerByte(level, 5), 435000000).level);
	assert.deepEqual(there, [0, 2, 5, 7, 8]);
	for (let level = 0; level < 5; level++) assert.equal(powerOn(five, powerByte(level, 5), 435000000).level, level);
	// byte 0 is the radio's own setting, unknown offline: only the factory level is named
	const d = powerOn(targets[1], 0, 145000000);
	assert.deepEqual(d, { level: null, label: null, isDefault: true, factory: "5W" });
	assert.equal(targets[1].power.defaultLevel, null);
	// a byte past the scale reads as its top, as the firmware clamps it
	assert.equal(powerOn(five, 0xff, 435000000).label, "5W");
	assert.equal(powerOn(nine, 0xff, 435000000).label, "5W");
});

test("a channel outside a radio's ranges, or in a mode its firmware lacks, fails on that radio only", () => {
	const m = empty();
	const air = newChannel({ name: "AIR", mode: "fm", rxHz: 119100000, txHz: 119100000, rxOnly: true });
	const p25 = newChannel({ name: "P25", mode: "dmr", kind: 2, rxHz: 446000000, txHz: 446000000 });
	const tx = newChannel({ name: "TX", mode: "fm", rxHz: 446000000, txHz: 300000000 });
	assert.deepEqual(channelOn(targets[2], air), []);
	assert.deepEqual(channelOn(targets[0], air).map((x) => x.code), ["target_rx"]);
	assert.deepEqual(channelOn(targets[2], p25).map((x) => x.code), ["target_mode"]);
	assert.deepEqual(channelOn(targets[0], tx).map((x) => x.code), ["target_tx"]);
	// a range only a setting turns on is a softer reason of its own
	const ext = newChannel({ name: "EXT", mode: "fm", rxHz: 230000000, txHz: 230000000, rxOnly: true });
	assert.deepEqual(channelOn(targets[2], ext).map((x) => x.code), ["target_rx_ext"]);
	assert.deepEqual(channelOn(targets[3], ext).map((x) => x.code), ["target_rx"]);
	m.channels = [air, p25, tx];
	const p = targetProblems(m, targets);
	const air_rx = p.find((x) => x.item === air && x.code === "target_rx");
	assert.equal(air_rx.params.radios, "Five Level, Mobile, Nine Level 10 W");
	assert.equal(p.filter((x) => x.item === p25).length, 1);
	assert.equal(p.find((x) => x.item === p25).params.radios, "Nine Level 5 W, Nine Level 10 W");
	assert.ok(p.every((x) => x.level === "warning"));
	assert.deepEqual(targetProblems(m, []), []);
	// failing on every radio names them all at once, when the caller gives the words
	const every = targetProblems(m, targets, "All of them").find((x) => x.item === tx);
	assert.equal(every.params.radios, "All of them");
	assert.equal(targetProblems(m, targets.slice(0, 1), "All of them").find((x) => x.item === tx).params.radios, "Five Level");
});

test("the channel form lists the power on each radio, and only for more than one", () => {
	const m = empty();
	const one = channelForm(m, t, { targets: targets.slice(0, 1) });
	assert.ok(!one.some((f) => f.key === "powerAcross"));
	const f = channelForm(m, t, { targets }).find((x) => x.key === "powerAcross");
	assert.ok(f, "the readout is in the form");
	assert.equal(f.show({ rxOnly: true }, () => "446.0"), false);
	assert.equal(f.show({ rxOnly: false }, () => "446.0"), true);
	assert.deepEqual(f.parse({}), []);
});

test("without one radio's names, a power step is named as a CSV file writes it", () => {
	const opts = powerOptions((k, p) => (p ? `${k}:${p.n}` : k));
	assert.deepEqual(opts.map(([v]) => v), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
	assert.equal(opts[2][1], "editor.power.level:2");
	assert.equal(opts[8][1], "editor.power.level:8");
	assert.equal(opts[9][1], "editor.power.high");
});
