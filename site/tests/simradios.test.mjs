// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The codeplug editor against simulated radios: tests/host/cpsSim built as each image of each radio with a codeplug,
// with that board's name, flash map, codeplug base, power levels and features (site/tests/fixtures.py sim_radios). The
// radio side is the firmware's own code (core/cps, core/codeplug, the power view), so these are the reads and writes a
// real radio would see, short of the radio's USB or cable. Each test says what the editor does and checks what the
// radio stored and how it reads it (cpsSim --detail).
import { test, afterEach } from "node:test";
import assert from "node:assert/strict";
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from "node:fs";
import { spawn, spawnSync } from "node:child_process";
import { join } from "node:path";
import { tmpdir } from "node:os";

import { concat } from "../static/js/flasher/bytes.js";
import { decode, encode, empty, newChannel, newContact } from "../static/js/codeplug/model.js";
import { setKind, syncModes } from "../static/js/codeplug/channelmodes.js";
import { CpsSerial, keepIdentity } from "../static/js/codeplug/transport.js";
import { radioTargets, targetFor, powerOn } from "../static/js/codeplug/targets.js";

const DIR = process.env.RFOUNDRY_SITE_FIXTURES || "build/site-tests";
const { sims } = JSON.parse(readFileSync(join(DIR, "sims.json"), "utf8"));
const { radios } = JSON.parse(readFileSync(join(DIR, "radios.json"), "utf8"));
const targets = radioTargets(radios);
const DEFAULT = 255;	// CODEPLUG_POWER_DEFAULT: the channel runs at the radio's own setting

// One simulated radio on a serial port: the program's stdin and stdout as a stream (buf, clear, take, write, close).
// close() ends it and returns its whole flash as the radio left it. flash: start from these bytes (a radio that holds a
// codeplug already). Setting s.cut to n pulls the cable after n more writes: the port is lost, as in a browser
const running = new Set();
function radio(sim, { flash = null, args = [] } = {}) {
	const d = mkdtempSync(join(tmpdir(), "rfoundry-simradio-"));
	const dump = join(d, "flash.bin");
	const pre = [];
	if (flash) {
		writeFileSync(join(d, "in.bin"), flash);
		pre.push("--image", join(d, "in.bin"));
	}
	const p = spawn(sim.program, [...pre, ...args, "--dump", dump], { stdio: ["pipe", "pipe", "ignore"] });
	running.add(p);
	p.on("close", () => running.delete(p));
	const s = {
		buf: new Uint8Array(0),
		clear() { s.buf = new Uint8Array(0); },
		take(n) {
			const o = s.buf.slice(0, n);
			s.buf = s.buf.slice(n);
			return o;
		},
		cut: Infinity,
		lost: false,
		async write(data) {
			if (s.cut-- <= 0) {
				// as the browser reports a port whose cable went: the stream is lost, the write fails
				s.lost = true;
				throw new DOMException("The device has been lost.", "NetworkError");
			}
			p.stdin.write(data);
		},
		async close() {
			const done = new Promise((r) => p.on("close", r));
			p.stdin.end();
			await done;
			const out = new Uint8Array(readFileSync(dump));
			rmSync(d, { recursive: true, force: true });
			return out;
		},
	};
	p.stdout.on("data", (b) => { s.buf = concat(s.buf, new Uint8Array(b)); });
	return s;
}

// What the radio reads from its flash, channel by channel: {number: {name, rx, tx, mode, power}} (cpsSim --detail; power
// is the board level the radio runs, DEFAULT for its own setting)
function radioReads(sim, flash) {
	const d = mkdtempSync(join(tmpdir(), "rfoundry-simradio-"));
	try {
		writeFileSync(join(d, "flash.bin"), flash);
		const r = spawnSync(sim.program, ["--image", join(d, "flash.bin"), "--detail"], { encoding: "utf8" });
		assert.equal(r.status, 0, r.stderr);
		const out = {};
		for (const m of r.stdout.matchAll(/^channel (\d+) "([^"]*)" rx (\d+) tx (\d+) mode (\d+) bw \d+ power (\d+)/gm)) {
			out[m[1]] = { name: m[2], rx: Number(m[3]), tx: Number(m[4]), mode: Number(m[5]), power: Number(m[6]) };
		}
		return out;
	} finally {
		rmSync(d, { recursive: true, force: true });
	}
}

// The editor's connect: identify, then the radio's whole codeplug read
async function connect(sim, stream) {
	const tr = new CpsSerial(stream, { cable: sim.cable });
	const { radio: r, probe } = await tr.identify(radios);
	return { tr, r, probe };
}

// The editor's write: the file onto what was read (the radio's identity kept), the plan, the write, the read back
// compared, then the radio told to load it
async function writeAs(tr, model, have) {
	const want = keepIdentity(encode(model), have);
	const res = await tr.write(want, have);
	await tr.verify(want, res.plan);
	await tr.finish();
	return want;
}

// One codeplug for every radio: each power step on 2 m and 70 cm, FM and DMR, a contact, a group list, a zone, and a
// P25, NXDN and YSF channel
function everyRadio() {
	const m = empty();
	m.general = { ...m.general, radioName: "Q0SIM", dmrId: 3101234 };
	const tg = newContact({ name: "Worldwide", id: 91, type: "group" });
	const local = newContact({ name: "Local 9", id: 9, type: "group", timeslot: 2 });
	m.contacts.push(tg, local);
	const list = { number: null, name: "Listen", contacts: [tg, local] };
	m.rxLists.push(list);
	for (let power = 0; power <= 10; power++) {
		m.channels.push(newChannel({ name: `2m P${power}`, rxHz: 146520000 + power * 12500, txHz: 146520000 + power * 12500, power }));
		m.channels.push(newChannel({ name: `70 P${power}`, rxHz: 446000000 + power * 12500, txHz: 446000000 + power * 12500, power,
			mode: power % 2 ? "dmr" : "fm", contact: power % 2 ? tg : null, rxList: power % 2 ? list : null, colorCode: 1, timeslot: 1 }));
	}
	m.channels.push(newChannel({ name: "Repeater", rxHz: 442100000, txHz: 447100000, txTone: "88.5", power: 9 }));
	m.channels.push(newChannel({ name: "Listen 2m", rxHz: 162550000, txHz: 162550000, rxOnly: true }));
	for (const [kind, name] of [[2, "P25 Simplex"], [4, "NXDN Simplex"], [103, "YSF Simplex"]]) {
		const c = newChannel({ name, rxHz: 441000000 + kind * 25000, txHz: 441000000 + kind * 25000, power: 6 });
		m.channels.push(c);
		setKind(m, c, kind);
	}
	m.channels.forEach((c, i) => { c.number = i + 1; });
	m.zones.push({ number: 1, name: "Everything", channels: m.channels.slice(0, 16) });
	syncModes(m);
	return m;
}

// What a codeplug holds that must come back from any radio unchanged (the radio's name and DMR ID are its own: a write
// keeps them, keepIdentity)
const summary = (m) => ({
	channels: m.channels.map((c) => [c.number, c.name, c.rxHz, c.txHz, c.mode, c.kind || 0, c.power, c.rxOnly, c.contact?.name ?? null,
		c.rxList?.name ?? null, c.txTone || ""]),
	contacts: m.contacts.map((c) => [c.name, c.id, c.type, c.timeslot]),
	zones: m.zones.map((z) => [z.name, z.channels.map((c) => c.name)]),
});

// a test that failed midway leaves its radio running: end it, so the run finishes
afterEach(() => {
	for (const p of running) p.kill();
	running.clear();
});

for (const sim of sims) {
	const target = targets.find((x) => x.key === sim.key);

	test(`${sim.key}: the editor knows the radio, its image and its power levels`, async () => {
		const s = radio(sim);
		const { tr, r, probe } = await connect(sim, s);
		await s.close();
		assert.equal(r.id, sim.radio);
		assert.equal(probe.board, sim.board_name);
		assert.ok(target, "a target for every simulated image");
		assert.equal(targetFor(targets, probe.board, r.id).key, sim.key, "Made For shows this image");
		// the power view, as the firmware writes it, gives the levels the board sources name
		assert.deepEqual(tr.power?.names, sim.profile.names);
		assert.deepEqual(tr.power.bands, sim.profile.bands.map((b) => ({ lo: b.lo, hi: b.hi, names: b.names })));
		assert.equal(tr.power.defaultLevel, sim.profile.default ?? sim.profile.names.length - 1, "no setting yet: the factory level");
		assert.equal(!!tr.fast, sim.features.includes("CPS_FAST"), "the fast protocol where the firmware has it");
	});

	test(`${sim.key}: a codeplug for every radio goes on, reads as the editor predicts, and comes back`, async () => {
		const model = everyRadio();
		const s = radio(sim);
		const { tr } = await connect(sim, s);
		const have = await tr.read({ iddb: false });
		await writeAs(tr, model, have);
		const flash = await s.close();

		// the radio's own reading of what it stored: each channel's level is the one the editor shows for this image
		const reads = radioReads(sim, flash);
		for (const c of model.channels) {
			const got = reads[c.number];
			assert.ok(got, `${c.name}: stored`);
			assert.deepEqual([got.name, got.rx, got.tx], [c.name, c.rxHz, c.txHz], `${c.name}: name and frequencies`);
			const want = powerOn(target, c.power, c.txHz);
			assert.equal(got.power, c.power ? want.level : DEFAULT, `${c.name}: power byte ${c.power} on ${sim.key}`);
		}

		// read back by the editor from the radio as it is now
		const again = radio(sim, { flash });
		const back = await connect(sim, again);
		const read = decode(await back.tr.read({ iddb: false }));
		await again.close();
		assert.deepEqual(summary(read), summary(model));
		assert.deepEqual([read.general.radioName, read.general.dmrId], [decode(have).general.radioName, decode(have).general.dmrId],
			"the radio keeps its own name and DMR ID");
	});

	test(`${sim.key}: an older radio without the fast protocol takes the same codeplug over the CPS`, async () => {
		const model = everyRadio();
		const s = radio(sim, { args: ["--features", "FM,DMR,P25,NXDN,YSF"] });
		const { tr } = await connect(sim, s);
		assert.equal(tr.fast, null);
		const have = await tr.read({ iddb: false });
		await writeAs(tr, model, have);
		const flash = await s.close();
		const reads = radioReads(sim, flash);
		assert.equal(Object.keys(reads).length, model.channels.length);
		const again = radio(sim, { flash });
		const back = await connect(sim, again);
		assert.deepEqual(summary(decode(await back.tr.read({ iddb: false }))), summary(model));
		await again.close();
	});

	test(`${sim.key}: only what changed is written, and the rest of the radio is untouched`, async () => {
		const model = everyRadio();
		const s = radio(sim);
		const { tr } = await connect(sim, s);
		await writeAs(tr, model, await tr.read({ iddb: false }));
		const first = await s.close();

		const s2 = radio(sim, { flash: first });
		const c2 = await connect(sim, s2);
		const have = await c2.tr.read({ iddb: false });
		const m = decode(have);
		m.channels.find((c) => c.name === "Repeater").name = "Repeater 2";
		const want = keepIdentity(encode(m), have);
		const plan = await c2.tr.plan(want, have);
		if (c2.tr.fast) assert.equal(plan.blocks, 1, "one channel renamed: one block");
		const res = await c2.tr.write(want, have);
		await c2.tr.verify(want, res.plan);
		await c2.tr.finish();
		const second = await s2.close();
		// outside the codeplug's room nothing moved: the settings, the radio's other data. The staging area is the fast
		// protocol's scratch space (a write lands there first), and the journal (two sectors) carries a rewrite of a sector the codeplug shares
		const map = sim.map;
		const skip = [[sim.base, sim.end], [map.staging_addr || 0, (map.staging_addr || 0) + (map.staging_size || 0)],
			[map.journal_addr, map.journal_addr + 0x2000]];
		for (let a = 0; a < first.length; a += 4096) {
			if (skip.some(([lo, hi]) => a >= lo && a < hi)) continue;
			const same = first.subarray(a, a + 4096).every((v, i) => v === second[a + i]);
			if (!same) assert.fail(`${sim.key}: flash at 0x${a.toString(16)} changed outside the codeplug`);
		}
		assert.equal(radioReads(sim, second)[m.channels.find((c) => c.name === "Repeater 2").number].name, "Repeater 2");
	});

	test(`${sim.key}: a cable pulled during a write leaves a radio that reads, and the write done again succeeds`, async () => {
		const before = everyRadio();
		const s0 = radio(sim);
		const c0 = await connect(sim, s0);
		await writeAs(c0.tr, before, await c0.tr.read({ iddb: false }));
		const start = await s0.close();

		const after = everyRadio();
		for (const c of after.channels) c.name = `New ${c.number}`;
		const s = radio(sim, { flash: start });
		const c = await connect(sim, s);
		const have = await c.tr.read({ iddb: false });
		const want = keepIdentity(encode(after), have);
		s.cut = 6;	// a few frames into the write
		await assert.rejects(c.tr.write(want, have).then((r) => c.tr.verify(want, r.plan)).then(() => c.tr.finish()));
		const mid = await s.close();

		// the radio boots and reads a codeplug: every channel there, the old names where it stages a write
		const names = Object.values(radioReads(sim, mid)).map((x) => x.name);
		assert.equal(names.length, before.channels.length, "every channel still there");
		if (sim.map.staging_size) assert.deepEqual(names, before.channels.map((x) => x.name), "staged: nothing changes before the whole write");

		// connected again, the same write goes on whole
		const s2 = radio(sim, { flash: mid });
		const c2 = await connect(sim, s2);
		await writeAs(c2.tr, after, await c2.tr.read({ iddb: false }));
		const done = radioReads(sim, await s2.close());
		assert.deepEqual(Object.values(done).map((x) => x.name), after.channels.map((x) => x.name));
	});

	test(`${sim.key}: Radio Default follows the radio's own power setting`, async () => {
		const level = 0;
		const s = radio(sim, { args: ["--master", String(level)] });
		const { tr } = await connect(sim, s);
		await s.close();
		assert.equal(tr.power.defaultLevel, level, "the editor sees the radio's setting, not the factory level");
	});
}

test("older firmware that reports another name for its radio still finds it", async () => {
	for (const sim of sims) {
		const r = radios.find((x) => x.id === sim.radio);
		const alias = r.codeplug.names.find((n) => !targets.some((x) => x.boardName.toLowerCase() === n.toLowerCase()));
		if (!alias) continue;
		const s = radio(sim, { args: ["--board-name", alias] });
		const { r: found, probe } = await connect(sim, s);
		await s.close();
		assert.equal(found.id, sim.radio, `${alias}`);
		assert.equal(targetFor(targets, probe.board, found.id)?.radio, sim.radio, `${alias}: Made For still shows the radio`);
	}
});

test("a codeplug read from one radio writes to every other radio, and each runs it at its own levels", async () => {
	const [first, ...rest] = sims;
	const s = radio(first);
	const c = await connect(first, s);
	await writeAs(c.tr, everyRadio(), await c.tr.read({ iddb: false }));
	const src = radio(first, { flash: await s.close() });
	const from = await connect(first, src);
	const model = decode(await from.tr.read({ iddb: false }));
	await src.close();
	for (const sim of rest) {
		const t = targets.find((x) => x.key === sim.key);
		const d = radio(sim);
		const to = await connect(sim, d);
		await writeAs(to.tr, model, await to.tr.read({ iddb: false }));
		const reads = radioReads(sim, await d.close());
		const top = model.channels.find((x) => x.name === "2m P10");
		assert.equal(reads[top.number].power, t.power.names.length - 1, `${sim.key}: step 10 is its top level`);
		const low = model.channels.find((x) => x.name === "70 P1");
		assert.equal(reads[low.number].power, 0, `${sim.key}: step 1 is its lowest level`);
	}
});

test("every simulated image is a radio the site offers, at the codeplug base its flash map gives", () => {
	assert.ok(sims.length > 0);
	for (const sim of sims) {
		const r = radios.find((x) => x.id === sim.radio);
		assert.equal(r.codeplug.base, sim.base, `${sim.key}: base`);
		assert.equal(r.codeplug.end, sim.end, `${sim.key}: end`);
	}
});
