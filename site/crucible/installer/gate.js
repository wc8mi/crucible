// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Behnke
// The front page's decisions, without the DOM, so the tests can run them.

// A computer with Web Serial and WebUSB: the update reads the radio over serial and writes it over USB. Phones are
// not offered at launch (ADR 001, answer 7), even where the browser has both.
export function canInstall(nav = navigator) {
	const mobile = nav.userAgentData ? nav.userAgentData.mobile : /Android|iPhone|iPad|Mobile/i.test(nav.userAgent || "");
	return Boolean(nav.serial && nav.usb) && !mobile;
}

// The screen to show for a requested one: the gate before anything, then the terms, then what was asked if the page
// has it (screens), else home
export function route(want, { browserOk, termsOk, screens }) {
	if (!browserOk) return "gate";
	if (!termsOk) return "terms";
	return want !== "gate" && want !== "terms" && screens.includes(want) ? want : "home";
}
