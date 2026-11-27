// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Behnke
// Crucible's front page (site/crucible/installer): the browser gate and the screen order
import { test } from "node:test";
import assert from "node:assert/strict";
import { canInstall, route } from "../crucible/installer/gate.js";

const desktop = { serial: {}, usb: {}, userAgentData: { mobile: false } };

test("the gate passes a computer with Web Serial and WebUSB only", () => {
	assert.equal(canInstall(desktop), true);
	assert.equal(canInstall({ ...desktop, serial: undefined }), false, "no Web Serial (Firefox, Safari)");
	assert.equal(canInstall({ ...desktop, usb: undefined }), false, "no WebUSB");
	assert.equal(canInstall({ ...desktop, userAgentData: { mobile: true } }), false, "Android Chrome: not at launch");
	assert.equal(canInstall({ serial: {}, usb: {}, userAgent: "Mozilla/5.0 (Linux; Android 14) Mobile" }), false);
	assert.equal(canInstall({ serial: {}, usb: {}, userAgent: "Mozilla/5.0 (Windows NT 10.0)" }), true);
});

test("the gate comes first, then the terms, then the screen asked for", () => {
	const screens = ["gate", "terms", "home", "install"];
	assert.equal(route("install", { browserOk: false, termsOk: true, screens }), "gate");
	assert.equal(route("install", { browserOk: true, termsOk: false, screens }), "terms");
	assert.equal(route("install", { browserOk: true, termsOk: true, screens }), "install");
	assert.equal(route("", { browserOk: true, termsOk: true, screens }), "home");
	assert.equal(route("terms", { browserOk: true, termsOk: true, screens }), "home", "accepted terms are not shown again");
	assert.equal(route("gate", { browserOk: true, termsOk: true, screens }), "home");
	assert.equal(route("nothing", { browserOk: true, termsOk: true, screens }), "home", "an unknown hash shows home");
});
