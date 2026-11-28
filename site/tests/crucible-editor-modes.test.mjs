// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The modes the Codeplug Editor offers on a page that serves only its own radios (data-targets="all", the Crucible
// Channels page): with no radio picked it offers what any of those radios runs, never every mode (trunking is in none
// of Crucible's builds), while RadioFoundry's page keeps "no radio: everything".
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";
import { empty } from "../static/js/codeplug/model.js";
import { offeredKinds, offeredTypes } from "../static/js/codeplug/modes.js";

const app = readFileSync(new URL("../static/js/editor/app.js", import.meta.url), "utf8");
const picked = app.slice(app.indexOf("function madeFor()"), app.indexOf("// The power levels the choices name"));
const feats = app.slice(app.indexOf("function features()"), app.indexOf("function refresh()"));

const TARGETS = [
	{ key: "a", radio: "a", features: ["FM", "DMR", "P25", "NXDN", "YSF", "DMR_AES"] },
	{ key: "b", radio: "b", features: ["FM", "DMR", "YSF"] },
];
function features(targetKey, dataTargets) {
	const ctx = { S: { conn: null, targets: TARGETS, targetKey }, root: { dataset: dataTargets ? { targets: dataTargets } : {} } };
	return runInNewContext(picked + feats + "features();", ctx);
}
const tokens = (rows) => rows.map((x) => x.token).sort();

test("no radio picked on a page of its own radios offers their modes, no trunking", () => {
	const f = features("", "all");
	assert.deepEqual([...f].sort(), [...new Set(TARGETS.flatMap((x) => x.features))].sort());
	const m = empty();
	assert.deepEqual(tokens(offeredKinds(m, f)), ["DMR_AES", "NXDN", "P25", "YSF"]);
	assert.ok(!tokens(offeredTypes(m, f)).some((x) => x.endsWith("_TRUNK")));
});

test("a radio picked still narrows the modes to its own", () => {
	assert.deepEqual([...features("b", "all")], ["FM", "DMR", "YSF"]);
	assert.deepEqual(tokens(offeredKinds(empty(), features("b", "all"))), ["YSF"]);
});

test("a page without data-targets keeps no radio meaning every mode", () => {
	assert.equal(features("", undefined), null);
	assert.ok(tokens(offeredKinds(empty(), null)).includes("DMR_TRUNK"));
});
