// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// A site's prebuilt releases (releases.js): the index read safely, the images that fit a radio, and which way an
// update goes.

import { test } from "node:test";
import assert from "node:assert/strict";
import { FORMAT, parseIndex, forRadio, hiddenTests, showTests, optionsMatch, direction, optionMismatches, checkManifest, noteBlocks, ReleaseError }
	from "../static/js/flasher/releases.js";

const img = (board, git, git_time, options = {}) => ({ board, options, git, git_time, size: 100,
	image: `releases/t/${board}.bin`, manifest: `releases/t/${board}.json` });

const INDEX = {
	format: FORMAT, repo: "owner/fork",
	releases: [
		{ tag: "a-1", name: "First", published: "2026-11-20T00:00:00Z", images: [img("boardx", "1111111", 100), img("boardo", "1111111", 100, { POWER: "5w" })] },
		{ tag: "a-3", name: "Test", prerelease: true, published: "2026-11-26T00:00:00Z", images: [img("boardx", "3333333", 300)] },
		{ tag: "a-2", name: "Second", published: "2026-11-24T00:00:00Z", images: [img("boardx", "2222222", 200)] },
	],
};

test("the index is read newest first", () => {
	const ix = parseIndex(INDEX);
	assert.equal(ix.repo, "owner/fork");
	assert.deepEqual(ix.releases.map((r) => r.tag), ["a-3", "a-2", "a-1"]);
});

test("another format is refused", () => {
	assert.throws(() => parseIndex({ format: "x", releases: [] }), ReleaseError);
	assert.throws(() => parseIndex(null), ReleaseError);
});

test("an image path that leaves the site or a link that isn't https is dropped", () => {
	const ix = parseIndex({ format: FORMAT, releases: [
		{ tag: "bad", url: "javascript:alert(1)", images: [{ ...img("boardx", "1", 1), image: "../../etc/x.bin" }] },
		{ tag: "abs", images: [{ ...img("boardx", "1", 1), manifest: "/x.json" }] },
		{ tag: "ok", url: "javascript:x", images: [img("boardx", "1", 1)] },
	] });
	assert.deepEqual(ix.releases.map((r) => r.tag), ["ok"]);
	assert.equal(ix.releases[0].url, "");
});

test("only images for the board and its exact options fit; tests hidden unless asked", () => {
	const ix = parseIndex(INDEX);
	assert.deepEqual(forRadio(ix, "boardx").map((e) => e.release.tag), ["a-2", "a-1"]);
	assert.deepEqual(forRadio(ix, "boardx", {}, { tests: true }).map((e) => e.release.tag), ["a-3", "a-2", "a-1"]);
	assert.equal(hiddenTests(ix, "boardx"), 1);
	assert.deepEqual(forRadio(ix, "boardo", { POWER: "5w" }).map((e) => e.release.tag), ["a-1"]);
	assert.deepEqual(forRadio(ix, "boardo", { POWER: "10w" }), []);
	assert.deepEqual(forRadio(ix, "boardo"), [], "an option the radio has must be chosen");
	assert.deepEqual(forRadio(ix, "nope"), []);
});

test("pre-releases show when asked or when the radio has no full release", () => {
	const ix = parseIndex(INDEX);
	assert.equal(showTests(ix, "boardx"), false);
	assert.equal(showTests(ix, "boardx", {}, true), true);
	const only = parseIndex({ format: FORMAT, releases: [{ tag: "p-1", prerelease: true, images: [img("boardx", "1111111", 100)] }] });
	assert.equal(showTests(only, "boardx"), true);
	assert.deepEqual(forRadio(only, "boardx", {}, { tests: showTests(only, "boardx") }).map((e) => e.release.tag), ["p-1"]);
});

test("options match only when equal both ways", () => {
	assert.ok(optionsMatch({}, {}));
	assert.ok(optionsMatch({ A: "1" }, { A: "1" }));
	assert.ok(!optionsMatch({ A: "1" }, {}));
	assert.ok(!optionsMatch({}, { A: "1" }));
	assert.ok(!optionsMatch({ A: "1" }, { A: "2" }));
});

test("direction: same commit, newer, older (a downgrade), unknown", () => {
	const probe = { git: "2222222", version: { git_time: 200 } };
	assert.equal(direction(probe, img("x", "2222222abc", 999)), "same");
	assert.equal(direction({ git: "2222222-dirty", version: {} }, img("x", "2222222", 0)), "same");
	assert.equal(direction(probe, img("x", "3333333", 300)), "newer");
	assert.equal(direction(probe, img("x", "1111111", 100)), "older");
	assert.equal(direction({ git: "2222222", version: {} }, img("x", "1111111", 100)), "unknown");
	assert.equal(direction(probe, img("x", "1111111", 0)), "unknown");
});

test("a manifest's options are compared both ways", () => {
	assert.deepEqual(optionMismatches({ build_options: { POWER: "5w" } }, { POWER: "5w" }), []);
	assert.deepEqual(optionMismatches({ build_options: { POWER: "5w" } }, { POWER: "10w" }), [["POWER", "10w", "5w"]]);
	assert.deepEqual(optionMismatches({ build_options: { POWER: "5w" } }, {}), [["POWER", null, "5w"]]);
	assert.deepEqual(optionMismatches({}, { POWER: "5w" }), [["POWER", "5w", null]]);
});

test("the manifest must be the listed image's", () => {
	const i = img("boardx", "2222222", 200);
	checkManifest(i, { board: "boardx", git: "2222222", image: { size: 100 } });
	assert.throws(() => checkManifest(i, { board: "boardy", git: "2222222", image: { size: 100 } }), ReleaseError);
	assert.throws(() => checkManifest(i, { board: "boardx", git: "9999999", image: { size: 100 } }), ReleaseError);
	assert.throws(() => checkManifest(i, { board: "boardx", git: "2222222", image: { size: 5 } }), ReleaseError);
});

test("notes become plain paragraphs and list items", () => {
	const blocks = noteBlocks("<!-- SPDX -->\r\nFirmware from `abc`.\r\n\r\n- Added **bold** [link](https://x)\r\n- Two\r\n\r\n## Head\r\ntext <b>x</b>");
	assert.deepEqual(blocks, [
		{ kind: "p", text: "Firmware from abc." },
		{ kind: "li", text: "Added bold link" },
		{ kind: "li", text: "Two" },
		{ kind: "p", text: "Head text <b>x</b>" },
	]);
});
