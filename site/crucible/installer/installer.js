// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Behnke
// Crucible's installer front page (ADR 001): the browser gate, the terms once per computer, the Update door. The hash
// names the screen. Update opens the shared flasher page (update.html); a first install is RadioFoundry's (a link).

import { canInstall, route } from "./gate.js";
import { nextPosts, pressesFor } from "./news.js";

const TERMS = "crucible-terms";		// "1" once the visitor accepted the terms on this computer
const THEME = "rfoundry-theme";		// shared with the update page (site.js)

const root = document.documentElement;

function accepted() {
	try { return localStorage.getItem(TERMS) === "1"; } catch (e) { return false; }
}

const NEWS = /^news-/;		// a post's anchor (#news-<date>-<slug>): shown under whichever screen, never a screen

function show() {
	const want = location.hash.slice(1);
	if (NEWS.test(want)) return showPost(want);
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

// More: the next posts from the feed, read once; the button goes after the last post. Focus moves to the first new post
const list = document.getElementById("news-list");
const more = document.getElementById("news-more");
let feed = null;

async function loadMore() {
	if (!feed) feed = await (await fetch(more.dataset.feed)).json();
	const shown = list.querySelectorAll("article").length;
	const { posts, done } = nextPosts(feed, shown);
	const tpl = document.createElement("template");
	tpl.innerHTML = posts.map((p) => p.html).join("\n");	// built from the repository's posts (crucible_site.py)
	const first = tpl.content.querySelector("article h3 a");
	list.append(tpl.content);
	if (done) more.remove();
	return first;
}

if (more) more.addEventListener("click", async () => {
	more.disabled = true;
	try {
		const first = await loadMore();
		more.textContent = "More news";
		first?.focus();
	} catch (e) {
		more.textContent = "The news did not load. Try again.";
	} finally {
		more.disabled = false;
	}
});

// A link to one post: the screen the visitor may see, then the post, with older posts loaded until it is there
async function showPost(id) {
	const all = [...document.querySelectorAll("[data-screen]")];
	const screen = route("home", { browserOk: canInstall(), termsOk: accepted(), screens: all.map((s) => s.dataset.screen) });
	for (const s of all) s.classList.toggle("show", s.dataset.screen === screen);
	if (!document.getElementById(id) && more?.isConnected && !more.disabled) {
		more.disabled = true;		// a press during these loads would add the same posts twice
		try {
			if (!feed) feed = await (await fetch(more.dataset.feed)).json();
			for (let n = pressesFor(feed, list.querySelectorAll("article").length, id); n > 0 && more.isConnected; n--) await loadMore();
		} catch (e) { /* the post stays unloaded; the page shows the newest */ }
		more.disabled = false;
	}
	document.getElementById(id)?.scrollIntoView();
}

addEventListener("hashchange", show);
show();
