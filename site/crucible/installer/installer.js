// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Behnke
// Crucible's installer front page (ADR 001): the browser gate, the terms once per computer, the Update door. The hash
// names the screen. Update opens the shared flasher page (update.html); a first install is RadioFoundry's (a link).

import { canInstall, route } from "./gate.js";

const TERMS = "crucible-terms";		// "1" once the visitor accepted the terms on this computer
const THEME = "rfoundry-theme";		// shared with the update page (site.js)

const root = document.documentElement;

function accepted() {
	try { return localStorage.getItem(TERMS) === "1"; } catch (e) { return false; }
}

function show() {
	const want = location.hash.slice(1);
	const all = [...document.querySelectorAll("[data-screen]")];
	const id = route(want, { browserOk: canInstall(), termsOk: accepted(), screens: all.map((s) => s.dataset.screen) });
	for (const s of all) s.classList.toggle("show", s.dataset.screen === id);
	const h1 = document.querySelector(`[data-screen="${id}"] h1`);
	if (want && h1) { h1.setAttribute("tabindex", "-1"); h1.focus({ preventScroll: true }); }
	scrollTo(0, 0);
}

const box = document.getElementById("terms-ok");
const go = document.getElementById("terms-go");
box.addEventListener("change", () => { go.disabled = !box.checked; });
go.addEventListener("click", () => {
	try { localStorage.setItem(TERMS, "1"); } catch (e) { /* private mode: asked again next time */ }
	location.hash = "home";
	show();
});

document.querySelector(".theme-toggle").addEventListener("click", () => {
	const dark = root.dataset.theme ? root.dataset.theme === "dark" : !matchMedia("(prefers-color-scheme: light)").matches;
	root.dataset.theme = dark ? "light" : "dark";
	try { localStorage.setItem(THEME, root.dataset.theme); } catch (e) { /* private mode: not remembered */ }
});

addEventListener("hashchange", show);
show();
