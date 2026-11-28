// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke

import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import { AMSAT_API, amsatEndpoint, buildTleBundle, CATALOG_ID_MAX, DEFAULT_SELECTED_IDS, filterFmSatellites, MAX_SELECTED_PROFILES,
	mergeNewest, parseTleCatalog, PE0SAT_API, PE0SAT_CATALOG, pe0satEndpoint, SATELLITES, tleCatalogField, tleCatalogId } from "../static/js/satellite-tle.js";

const SAMPLE = `ISS (ZARYA)\n1 25544U 98067A   26317.20662175  .00008559  00000+0  16132-3 0  9990\n2 25544  51.6325 282.6584 0007349  20.2588 339.8690 15.49346920594859\nSO-50\n1 27607U 02058C   26316.49281663  .00001582  00000+0  20311-3 0  9999\n2 27607  64.5539  30.0380 0068124 235.0518 124.4178 14.83333417286454\nAO-91\n1 43017U 17073E   26317.60524444  .00008151  00000+0  33970-3 0  9990\n2 43017  97.4443 181.7665 0144887 171.0350 189.3523 15.14807395487992\n`;

test("site satellite profiles match the firmware frequency table", async () => {
	const source = await readFile(new URL("../../core/satellite/frequency.c", import.meta.url), "utf8");
	const firmware = [...source.matchAll(/\{\s*(\d+)u,\s*"[^"]+",\s*(\d+)u,\s*(\d+)u,\s*(\d+)u\s*\}/g)]
		.map((m) => m.slice(1).map(Number));
	assert.deepEqual(SATELLITES.map(({ id, rxHz, txHz, toneHz }) => [id, rxHz, txHz, toneHz]), firmware);
	assert.deepEqual(DEFAULT_SELECTED_IDS, [25544, 27607, 43017]);
});

test("the AMSAT button uses the page's same-origin proxy path, or the default", async () => {
	assert.equal(AMSAT_API, "/api/amsat-tle");
	const page = "https://tinker.example/crucible/tools/satellite/index.html";
	for (const none of [undefined, null, "", "  "]) assert.equal(amsatEndpoint(none, page), AMSAT_API);
	assert.equal(amsatEndpoint("../../api/amsat-tle", page), "../../api/amsat-tle");
	assert.equal(amsatEndpoint("/crucible/api/amsat-tle", page), "/crucible/api/amsat-tle");
	assert.equal(amsatEndpoint("https://tinker.example/crucible/api/amsat-tle", page), "https://tinker.example/crucible/api/amsat-tle");
	for (const other of ["https://www.amsat.org/tle/daily-bulletin.txt", "//evil.example/x", "javascript:alert(1)",
		"data:text/plain,x", "/\\evil.example/x", "/\t/evil.example/x", "/\n/evil.example/x", "\\\\evil.example/x",
		"http://tinker.example/crucible/api/amsat-tle", "https://tinker.example:8443/x"])
		assert.equal(amsatEndpoint(other, page), AMSAT_API, JSON.stringify(other));
	for (const ws of ["\u00a0", "\ufeff", "\u2028"])
		assert.equal(amsatEndpoint(ws + "//evil.example/x", page), AMSAT_API, "a leading space that trim() strips");
	assert.equal(amsatEndpoint("../../api/amsat-tle", undefined), AMSAT_API, "no page to compare with: the default");
	const tool = await readFile(new URL("../static/js/tletool.js", import.meta.url), "utf8");
	assert.match(tool, /amsatEndpoint\(form\?\.dataset\.amsatApi\)/);
	assert.match(tool, /pe0satEndpoint\(form\?\.dataset\.pe0satApi\)/);
	assert.match(tool, /fetch\(source\.api, /);
	assert.doesNotMatch(tool, /fetch\("/, "every fetch goes through the endpoint");
});

test("only known FM satellites are included in the selectable catalog", () => {
	const records = [...SATELLITES.map(({ id, name }) => ({ catalog_id: id, name })),
		{ catalog_id: 99001, name: "Non-FM test object" }];
	assert.deepEqual(filterFmSatellites(records).map(({ catalog_id }) => catalog_id), SATELLITES.map(({ id }) => id));
});

test("AMSAT proxy requests and serves uncached bulletins", async () => {
	const source = await readFile(new URL("../../functions/api/amsat-tle.js", import.meta.url), "utf8");
	const { onRequestGet } = await import(`data:text/javascript;base64,${Buffer.from(source).toString("base64")}`);
	const oldFetch = globalThis.fetch;
	try {
		globalThis.fetch = async (url, options) => {
			assert.equal(url, "https://www.amsat.org/tle/daily-bulletin.txt");
			assert.equal(options.cache, "no-store");
			return new Response(SAMPLE);
		};
		const response = await onRequestGet();
		assert.equal(response.headers.get("cache-control"), "no-store");
		assert.equal(await response.text(), SAMPLE);
		globalThis.fetch = async () => new Response("", { status: 503 });
		assert.equal((await onRequestGet()).status, 502);
	} finally {
		globalThis.fetch = oldFetch;
	}
});

test("the PE0SAT endpoint takes a same-origin path only", () => {
	const page = "https://tinker.example/crucible/tools/satellite/index.html";
	assert.equal(PE0SAT_API, "/api/pe0sat-tle");
	assert.equal(pe0satEndpoint("../../api/pe0sat-tle", page), "../../api/pe0sat-tle");
	for (const other of [PE0SAT_CATALOG, "//evil.example/x", "http://tinker.example/crucible/api/pe0sat-tle"])
		assert.equal(pe0satEndpoint(other, page), PE0SAT_API, other);
	assert.equal(pe0satEndpoint("", page), PE0SAT_API);
});

test("PE0SAT proxy requests and serves uncached lists", async () => {
	const source = await readFile(new URL("../../functions/api/pe0sat-tle.js", import.meta.url), "utf8");
	const { onRequestGet } = await import(`data:text/javascript;base64,${Buffer.from(source).toString("base64")}`);
	const oldFetch = globalThis.fetch;
	try {
		globalThis.fetch = async (url, options) => {
			assert.equal(url, PE0SAT_CATALOG);
			assert.equal(options.cache, "no-store");
			return new Response(SAMPLE);
		};
		const response = await onRequestGet();
		assert.equal(response.headers.get("cache-control"), "no-store");
		assert.equal(await response.text(), SAMPLE);
		globalThis.fetch = async () => new Response("", { status: 503 });
		assert.equal((await onRequestGet()).status, 502);
	} finally {
		globalThis.fetch = oldFetch;
	}
});

test("the page refreshes AMSAT before export and retains checked satellites through search", async () => {
	class Element {
		constructor() { this.children = []; this.listeners = {}; this.value = ""; this.dataset = {}; }
		addEventListener(type, handler) { this.listeners[type] = handler; }
		async fire(type) { return this.listeners[type]?.({ target: this }); }
		append(...children) { this.children.push(...children); }
		replaceChildren(...children) { this.children = children; }
		querySelectorAll(selector) {
			const descendants = this.children.flatMap((child) => [child, ...child.querySelectorAll("*")]);
			return selector === "*" ? descendants : descendants.filter((child) =>
				child.type === "checkbox" && (selector !== "input:checked" || child.checked));
		}
		click() {}
	}
	const ids = ["satellite-tle-file", "tle-latest", "satellite-tle-options", "satellite-tle-build",
		"satellite-tle-push", "satellite-tle-status", "tle-provider", "satellite-tle-search"];
	const elements = Object.fromEntries(ids.map((id) => [id, new Element()]));
	const strings = await readFile(new URL("../i18n/en.json", import.meta.url), "utf8");
	const oldDocument = globalThis.document, oldFetch = globalThis.fetch;
	const oldCreate = URL.createObjectURL, oldRevoke = URL.revokeObjectURL, oldTimeout = globalThis.setTimeout;
	const navigatorDescriptor = Object.getOwnPropertyDescriptor(globalThis, "navigator");
	const downloads = [], requests = [];
	const storage = new Map();
	const storageDescriptor = Object.getOwnPropertyDescriptor(globalThis, "localStorage");
	let linkModule, savedLink;
	let responseText;
	const catalogAt = (daysAgo) => {
		const date = new Date(Date.now() - daysAgo * 86400000);
		const day = Math.floor((Date.UTC(date.getUTCFullYear(), date.getUTCMonth(), date.getUTCDate()) -
			Date.UTC(date.getUTCFullYear(), 0, 1)) / 86400000) + 1;
		const epoch = `${String(date.getUTCFullYear() % 100).padStart(2, "0")}${String(day).padStart(3, "0")}.00000000`;
		return SAMPLE.split("\n").map((line) => {
			if (!line.startsWith("1 ")) return line;
			const body = line.slice(0, 18) + epoch + line.slice(32, 68);
			const sum = [...body].reduce((total, char) =>
				total + (char >= "0" && char <= "9" ? Number(char) : char === "-" ? 1 : 0), 0);
			return body + sum % 10;
		}).join("\n");
	};
	try {
		globalThis.document = {
			getElementById: (id) => id === "i18n" ? { textContent: strings } : elements[id] || null,
			createElement: () => new Element(),
		};
		globalThis.fetch = async (url, options) => {
			requests.push({ url, options });
			return new Response(responseText);
		};
		URL.createObjectURL = (blob) => { downloads.push(blob); return "blob:test"; };
		URL.revokeObjectURL = () => {};
		globalThis.setTimeout = (fn) => { fn(); return 0; };
		Object.defineProperty(globalThis, "localStorage", { configurable: true, value: {
			getItem: (key) => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, value),
		} });
		await import("../static/js/tletool.js");
		const startRows = elements["satellite-tle-options"].children;
		const row = (id) => startRows.find((item) => item.dataset.id === String(id));
		assert.equal(startRows.length, SATELLITES.length, "before a list loads, every built-in satellite is offered");
		assert.match(row(59112).className, /tle-profile-listen/);
		assert.match(row(59112).children[2].textContent, /RX 145\.880 MHz · listen only, no TX/);
		assert.doesNotMatch(row(25544).className, /tle-profile-listen/);
		assert.match(row(25544).children[2].textContent, /TX 145\.990 MHz/);
		responseText = catalogAt(8);
		const fetchCatalog = globalThis.fetch;
		const pending = [];
		globalThis.fetch = (...args) => new Promise((resolve) => {
			pending.push(async () => resolve(await fetchCatalog(...args)));
		});
		const loading = elements["tle-latest"].fire("click");
		assert.ok(elements["satellite-tle-build"].disabled && elements["satellite-tle-push"].disabled &&
			elements["satellite-tle-file"].disabled);
		assert.equal(pending.length, 2, "both lists are fetched at once");
		for (const resolve of pending) await resolve();
		await loading;
		globalThis.fetch = fetchCatalog;
		elements["satellite-tle-search"].value = "ISS";
		await elements["satellite-tle-search"].fire("input");
		responseText = catalogAt(0);
		await elements["satellite-tle-build"].fire("click");
		const bundle = JSON.parse(await downloads[0].text());
		assert.deepEqual(bundle.profile_ids, [25544, 27607, 43017]);
		assert.equal(bundle.records[0].line1, parseTleCatalog(responseText)[0].line1);
		assert.equal(requests.length, 4);
		assert.deepEqual(requests.map(({ url }) => url), [AMSAT_API, PE0SAT_API, AMSAT_API, PE0SAT_API]);
		assert.ok(requests.every(({ options }) => options.cache === "no-store"));

		responseText = catalogAt(0).split("AO-91\n")[0];
		await elements["satellite-tle-build"].fire("click");
		assert.equal(downloads.length, 1);
		assert.match(elements["satellite-tle-status"].textContent, /missing selected IDs: 43017/);

		elements["satellite-tle-search"].value = "";
		await elements["satellite-tle-search"].fire("input");
		for (const checkbox of elements["satellite-tle-options"].querySelectorAll("input")) {
			if (checkbox.value !== "25544") {
				checkbox.checked = false;
				await checkbox.fire("change");
			}
		}
		linkModule = await import("../static/js/flasher/link.js");
		savedLink = { open: linkModule.SerialStream.open, version: linkModule.Link.prototype.version,
			chip: linkModule.Link.prototype.chip, request: linkModule.Link.prototype.request };
		let opened = 0, transferred = [];
		Object.defineProperty(globalThis, "navigator", { configurable: true, value: {
			serial: { requestPort: async () => ({}) },
		} });
		linkModule.SerialStream.open = async () => { opened++; return { close: async () => {} }; };
		linkModule.Link.prototype.version = async () => ({ board: "test" });
		linkModule.Link.prototype.chip = async () => null;
		linkModule.Link.prototype.request = async (command, payload) => {
			if (command === 0x09) {
				const text = new TextEncoder().encode("section=core\nbands=136000000-174000000,400000000-480000000\n");
				const reply = new Uint8Array(5 + text.length);
				reply[4] = 1;
				reply.set(text, 5);
				return reply;
			}
			switch (payload[0]) {
			case 0: return new Uint8Array([7, 10, 0, 1, 0]);
			case 9: return new Uint8Array([1, 0]);
			case 3: {
				transferred.push(...payload.slice(5));
				return new Uint8Array([transferred.length & 255, transferred.length >> 8]);
			}
			case 12: case 4: return new Uint8Array(0);
			case 1: {
				const reply = new Uint8Array(9), view = new DataView(reply.buffer);
				reply[0] = 1;
				view.setUint32(1, 25544, true);
				const date = new Date();
				view.setUint32(5, Date.UTC(date.getUTCFullYear(), date.getUTCMonth(), date.getUTCDate()) / 1000, true);
				return reply;
			}
			case 11: {
				const reply = new Uint8Array(15), view = new DataView(reply.buffer);
				reply[0] = 1;
				view.setUint32(1, 25544, true);
				view.setUint32(5, 437800000, true);
				view.setUint32(9, 145990000, true);
				view.setUint16(13, 670, true);
				return reply;
			}
			default: throw new Error(`unexpected operation ${payload[0]}`);
			}
		};
		responseText = catalogAt(0);
		await elements["satellite-tle-push"].fire("click");
		assert.equal(opened, 1);
		assert.match(elements["satellite-tle-status"].textContent, /profiles synced/);
		assert.ok(new TextDecoder().decode(Uint8Array.from(transferred)).includes(parseTleCatalog(responseText)[0].line1));
		assert.equal(requests.length, 8);

		globalThis.fetch = async () => new Response("", { status: 404 });
		await elements["satellite-tle-build"].fire("click");
		assert.equal(downloads.length, 1);
		assert.match(elements["satellite-tle-status"].textContent, /AMSAT API route was not found/);
		await elements["satellite-tle-push"].fire("click");
		assert.equal(opened, 1);
		assert.match(elements["satellite-tle-status"].textContent, /AMSAT API route was not found/);
		const fileText = catalogAt(1);
		elements["satellite-tle-file"].files = [{ size: fileText.length, text: async () => fileText }];
		await elements["satellite-tle-file"].fire("change");
		await elements["satellite-tle-build"].fire("click");
		assert.equal(downloads.length, 2);
		assert.equal(JSON.parse(await downloads[1].text()).records[0].line1, parseTleCatalog(fileText)[0].line1);

		// Merged: per satellite the newest lines, AMSAT's name where it has one, the page as the bundle's provider
		const amsatText = catalogAt(8).split("AO-91\n")[0];
		// PE0SAT names some objects differently (IO-86 is LAPAN-A2 there): the box shows both names, search finds either
		const pe0satText = catalogAt(0).replace("SO-50\n", "SAUDISAT 1C\n").replace("AO-91\n", "FOX-1B\n");
		const listFetch = (amsat, pe0sat) => async (url) => {
			const reply = url === AMSAT_API ? amsat : pe0sat;
			return typeof reply === "number" ? new Response("", { status: reply }) : new Response(reply);
		};
		globalThis.fetch = listFetch(amsatText, pe0satText);
		const page = "https://tinker.example/crucible/tools/satellite/index.html";
		const locationDescriptor = Object.getOwnPropertyDescriptor(globalThis, "location");
		Object.defineProperty(globalThis, "location", { configurable: true, value: { href: `${page}?x=1#top` } });
		try {
			await elements["tle-latest"].fire("click");
		} finally {
			if (locationDescriptor) Object.defineProperty(globalThis, "location", locationDescriptor);
			else delete globalThis.location;
		}
		assert.equal(elements["tle-provider"].value, page);
		const rows = () => elements["satellite-tle-options"].children.filter((row) => row.dataset?.id);
		const labels = () => rows().map((row) => row.children[1].textContent);
		assert.deepEqual(labels(), ["ISS (ZARYA)", "SO-50", "AO-91 (FOX-1B)"]);
		assert.ok(rows().every((row) => /TLE <1 d old \(PE0SAT\)$/.test(row.children[2].textContent)),
			rows().map((row) => row.children[2].textContent).join("; "));
		for (const query of ["ao-91", "fox", "43017"]) {
			elements["satellite-tle-search"].value = query;
			elements["satellite-tle-options"].replaceChildren();
			await elements["satellite-tle-search"].fire("input");
			assert.deepEqual(labels(), ["AO-91 (FOX-1B)"], query);
		}
		elements["satellite-tle-search"].value = "";
		await elements["satellite-tle-search"].fire("input");
		for (const checkbox of elements["satellite-tle-options"].querySelectorAll("input")) {
			if (checkbox.value === "43017" && !checkbox.checked) {
				checkbox.checked = true;
				await checkbox.fire("change");
			}
		}
		assert.deepEqual(JSON.parse(storage.get("satellite-tle-picks")).sort(), [25544, 43017]);
		await elements["satellite-tle-build"].fire("click");
		assert.equal(downloads.length, 3, elements["satellite-tle-status"].textContent);
		const merged = JSON.parse(await downloads[2].text());
		assert.equal(merged.provider, page);
		assert.equal(merged.records[0].line1, parseTleCatalog(pe0satText)[0].line1, "the newer lines");
		assert.equal(merged.records[0].name, "ISS (ZARYA)");

		// one list down: the other is used, and the status says so
		globalThis.fetch = listFetch(503, pe0satText);
		await elements["tle-latest"].fire("click");
		assert.match(elements["satellite-tle-status"].textContent, /Using PE0SAT only\. Could not fetch the AMSAT bulletin: 503/);
		assert.ok(!elements["satellite-tle-build"].disabled);
		globalThis.fetch = listFetch(404, 404);
		await elements["tle-latest"].fire("click");
		assert.match(elements["satellite-tle-status"].textContent, /AMSAT API route was not found.*PE0SAT API route was not found/);
		assert.ok(elements["satellite-tle-build"].disabled);
	} finally {
		if (savedLink) {
			linkModule.SerialStream.open = savedLink.open;
			linkModule.Link.prototype.version = savedLink.version;
			linkModule.Link.prototype.chip = savedLink.chip;
			linkModule.Link.prototype.request = savedLink.request;
		}
		if (navigatorDescriptor) Object.defineProperty(globalThis, "navigator", navigatorDescriptor);
		else delete globalThis.navigator;
		if (storageDescriptor) Object.defineProperty(globalThis, "localStorage", storageDescriptor);
		else delete globalThis.localStorage;
		globalThis.document = oldDocument;
		globalThis.fetch = oldFetch;
		URL.createObjectURL = oldCreate;
		URL.revokeObjectURL = oldRevoke;
		globalThis.setTimeout = oldTimeout;
	}
});

test("parses a named TLE catalog and builds the selected JSON bundle", () => {
	const catalog = parseTleCatalog(`Daily bulletin\n\n${SAMPLE}`);
	const now = new Date("2026-11-14T00:00:00Z");
	const bundle = buildTleBundle(catalog, [43017, 25544], { now });
	assert.deepEqual(Object.keys(bundle), ["schema_version", "provider", "fetched_at", "profile_ids", "profiles", "records"]);
	assert.equal(bundle.schema_version, 3);
	assert.deepEqual(bundle.profile_ids, [25544, 43017]);
	assert.deepEqual(bundle.profiles.map(({ catalog_id }) => catalog_id), [25544, 43017]);
	assert.equal(bundle.fetched_at, "2026-11-14T00:00:00Z");
	assert.deepEqual(bundle.records.map((record) => record.catalog_id), [25544, 43017]);
	assert.equal(bundle.records[0].name, "ISS (ZARYA)");
	assert.equal(SATELLITES.find(({ id }) => id === 25544).name, "ISS");
});

test("rejects malformed, stale, and unselected catalog data", () => {
	const catalog = parseTleCatalog(SAMPLE);
	assert.throws(() => buildTleBundle(catalog, []), /Select at least one satellite/);
	assert.throws(() => buildTleBundle(catalog, [40908]), /missing selected IDs/);
	assert.equal(MAX_SELECTED_PROFILES, 18);
	assert.throws(() => buildTleBundle(catalog, [...SATELLITES.map(({ id }) => id), 99001, 99002],
		{ now: new Date("2026-11-14T00:00:00Z") }), /no more than 18/);
	const badChecksum = catalog.map((record) => ({ ...record }));
	badChecksum[0].line1 = badChecksum[0].line1.slice(0, 68) + (badChecksum[0].line1[68] === "0" ? "1" : "0");
	assert.throws(() => buildTleBundle(badChecksum, [25544], { now: new Date("2026-11-14T00:00:00Z") }), /checksum/);
	assert.throws(() => buildTleBundle(catalog, [25544], { now: new Date("2027-01-01T00:00:00Z") }), /30-day age limit/);
	assert.throws(() => parseTleCatalog("Name\n1 25544\n"), /incomplete TLE pair/);
});

test("a repeated catalog ID keeps the first of identical lines and leaves out differing ones", () => {
	const [iss1, iss2, so50Name, so501, so502] = SAMPLE.split("\n").slice(1, 6);
	// two objects published under one element set, as in PE0SAT's list
	const shared = parseTleCatalog(`${SAMPLE}ISS COPY\n${iss1}\n${iss2}\n`);
	assert.deepEqual(shared.map(({ catalog_id }) => catalog_id), [25544, 27607, 43017]);
	assert.equal(shared[0].name, "ISS (ZARYA)");
	// two element sets for one ID: that ID is left out, the rest stays usable
	const other = iss2.replace("51.6325", "51.6326");
	const conflict = parseTleCatalog(`${SAMPLE}ISS OTHER\n${iss1}\n${other}\n`);
	assert.deepEqual(conflict.map(({ catalog_id }) => catalog_id), [27607, 43017]);
	assert.throws(() => buildTleBundle(conflict, [25544], { now: new Date("2026-11-14T00:00:00Z") }), /missing selected IDs: 25544/);
	assert.throws(() => parseTleCatalog(`ISS\n${iss1}\n${iss2}\nISS B\n${iss1}\n${other}\n`), /No TLE records/);
	assert.equal(so50Name, "SO-50");
	assert.ok(so501 && so502);
});

test("merging keeps each satellite's newest lines, the first list's name, and the source", () => {
	const [name, line1, line2] = SAMPLE.split("\n");
	const newer = parseTleCatalog(`ISS NEW NAME\n${line1.replace("26317.20662175", "26318.20662175").slice(0, 68)}0\n${line2}\n`);
	newer[0].line1 = newer[0].line1.slice(0, 68) + [...newer[0].line1.slice(0, 68)].reduce((sum, c) =>
		sum + (c >= "0" && c <= "9" ? Number(c) : c === "-" ? 1 : 0), 0) % 10;
	const merged = mergeNewest([{ source: "AMSAT", records: parseTleCatalog(SAMPLE) }, { source: "PE0SAT", records: newer }]);
	assert.deepEqual(merged.map(({ catalog_id, source }) => [catalog_id, source]),
		[[25544, "PE0SAT"], [27607, "AMSAT"], [43017, "AMSAT"]]);
	assert.equal(merged[0].name, name);
	assert.equal(merged[0].line1, newer[0].line1);
	const older = mergeNewest([{ source: "AMSAT", records: newer }, { source: "PE0SAT", records: parseTleCatalog(SAMPLE) }]);
	assert.equal(older[0].source, "AMSAT", "an older copy does not replace a newer one");
});

test("an Alpha-5 catalog number reads as six digits, a malformed one is rejected", () => {
	const alpha = "NEW OBJECT\n1 A0240U 26179A   26331.58728273  .00000456  00000-0  42087-4 0  9998\n" +
		"2 A0240  97.4000 100.0000 0010000  90.0000 270.0000 15.00000000  1234\n";
	assert.deepEqual(parseTleCatalog(alpha + SAMPLE).map(({ catalog_id }) => catalog_id), [100240, 25544, 27607, 43017]);
	assert.throws(() => parseTleCatalog(alpha.replaceAll("A0240", "I0240") + SAMPLE), /invalid NORAD catalog ID/);
	assert.throws(() => parseTleCatalog(alpha.replaceAll("A0240", "O0240") + SAMPLE), /invalid NORAD catalog ID/);
	for (const [field, id] of [["00005", 5], ["A1101", 101101], ["H9999", 179999], ["J0000", 180000],
		["P0000", 230000], ["Z9999", CATALOG_ID_MAX]]) {
		assert.equal(tleCatalogId(field), id);
		assert.equal(tleCatalogField(id), field);
	}
	assert.equal(tleCatalogId("I0000"), null);
});

test("a six-digit, listen-only satellite builds a bundle; a listen-only profile can't carry a tone", () => {
	const line = (text) => {
		const body = text.padEnd(68).slice(0, 68);
		let sum = 0;
		for (const char of body) sum += char >= "0" && char <= "9" ? Number(char) : char === "-" ? 1 : 0;
		return body + (sum % 10);
	};
	const catalog = [{ catalog_id: 101102, name: "HADES-L",
		line1: line("1 A1102U 25135BV  26317.20662175  .00008559  00000+0  16132-3 0  999"),
		line2: line("2 A1102  97.4325 282.6584 0007349  20.2588 339.8690 15.4934692059485") }];
	assert.deepEqual(parseTleCatalog(`HADES-L\n${catalog[0].line1}\n${catalog[0].line2}\n`), catalog);
	const now = new Date("2026-11-14T00:00:00Z");
	const bundle = buildTleBundle(catalog, [101102], { now });
	assert.deepEqual(bundle.profiles, [{ catalog_id: 101102, rx_hz: 436665000, tx_hz: 0, tx_tone_tenths: 0 }]);
	assert.throws(() => buildTleBundle(catalog, [101102], { now,
		profiles: [{ catalog_id: 101102, rx_hz: 436665000, tx_hz: 0, tx_tone_tenths: 670 }] }), /enter valid RX/);
	const listen = SATELLITES.filter((satellite) => satellite.txHz === 0);
	assert.ok(listen.length >= 10 && listen.every((satellite) => satellite.toneHz === 0));
});

test("accepts bulletin satellites with manually supplied frequency profiles", () => {
	const customText = `Manual satellite\n1 99001U 26001A   26317.20662175  .00008559  00000+0  16132-3 0  9998\n2 99001  51.6325 282.6584 0007349  20.2588 339.8690 15.49346920594858\n`;
	const catalog = parseTleCatalog(customText);
	const profile = { catalog_id: 99001, rx_hz: 145900000, tx_hz: 435100000, tx_tone_tenths: 885 };
	const bundle = buildTleBundle(catalog, [99001], {
		now: new Date("2026-11-14T00:00:00Z"), profiles: [profile],
	});
	assert.deepEqual(bundle.profiles, [profile]);
	assert.throws(() => buildTleBundle(catalog, [99001], { profiles: [] }), /enter valid RX/);
	assert.throws(() => buildTleBundle(catalog, [99001], {
		profiles: [{ ...profile, tx_hz: 1000000000 }],
	}), /enter valid RX/);
});