// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Prebuilt releases a site offers for the update (a page with data-releases: the index it names). Which of them fit
// the radio, which one it runs, and whether one is older than what it runs. No DOM, so the Node tests can use it.
//
// The index (release-index/1), written by the site build that copies the images next to it:
//   {format, repo, releases: [{tag, name, prerelease, published, url, notes,
//                              images: [{board, options: {NAME: value}, image, manifest, git, git_time, size}]}]}
// image and manifest are paths relative to the index.

export const FORMAT = "release-index/1";

export class ReleaseError extends Error {}

const str = (x) => typeof x === "string" && x.length > 0;
const SAFE_PATH = /^[A-Za-z0-9._\-/]+$/;
const safePath = (p) => str(p) && SAFE_PATH.test(p) && !p.split("/").includes("..") && !p.startsWith("/");

// The index with every release and image it can use; anything malformed is left out, not trusted
export function parseIndex(json) {
	if (!json || json.format !== FORMAT || !Array.isArray(json.releases)) throw new ReleaseError(`not a ${FORMAT} index`);
	const releases = [];
	for (const r of json.releases) {
		if (!r || !str(r.tag)) continue;
		const images = (Array.isArray(r.images) ? r.images : []).filter((i) => i && str(i.board) && safePath(i.image) && safePath(i.manifest))
			.map((i) => ({ board: i.board, options: i.options && typeof i.options === "object" ? { ...i.options } : {}, image: i.image,
				manifest: i.manifest, git: str(i.git) ? i.git : "", git_time: Number(i.git_time) || 0, size: Number(i.size) || 0 }));
		if (!images.length) continue;
		releases.push({ tag: r.tag, name: str(r.name) ? r.name : r.tag, prerelease: !!r.prerelease, published: str(r.published) ? r.published : "",
			url: str(r.url) && /^https:\/\//.test(r.url) ? r.url : "", notes: typeof r.notes === "string" ? r.notes : "", images });
	}
	// newest first: by publication, then by commit time
	releases.sort((a, b) => (b.published || "").localeCompare(a.published || "") || (b.images[0].git_time - a.images[0].git_time));
	return { repo: str(json.repo) ? json.repo : "", releases };
}

// Whether an image's build options are exactly the radio's: every option the radio has, with its value, and none other
export function optionsMatch(imageOptions, chosen) {
	const a = imageOptions || {}, b = chosen || {};
	const keys = new Set([...Object.keys(a), ...Object.keys(b)]);
	for (const k of keys) if (a[k] !== b[k]) return false;
	return true;
}

// The releases that have an image for this board and these options: [{release, image}], newest first. Pre-releases
// only with tests
export function forRadio(index, board, options = {}, { tests = false } = {}) {
	const out = [];
	for (const release of index.releases) {
		if (release.prerelease && !tests) continue;
		const image = release.images.find((i) => i.board === board && optionsMatch(i.options, options));
		if (image) out.push({ release, image });
	}
	return out;
}

// Whether the list shows pre-releases: when asked, or when the radio has no full release yet (a project that only
// publishes pre-releases would otherwise offer nothing)
export function showTests(index, board, options = {}, asked = false) {
	return asked || forRadio(index, board, options).length === 0;
}

// How many pre-releases for this radio the list hides
export function hiddenTests(index, board, options = {}) {
	return forRadio(index, board, options, { tests: true }).filter((e) => e.release.prerelease).length;
}

const gitSame = (a, b) => {
	const x = String(a || "").replace(/-dirty$/, ""), y = String(b || "").replace(/-dirty$/, "");
	return x.length >= 7 && y.length >= 7 && (x.startsWith(y) || y.startsWith(x));
};

// What an image is to the build on the radio (probe: the debug link's VERSION, {git, version: {git_time}}):
//   same     the radio runs this commit
//   newer    a later commit
//   older    an earlier commit: a downgrade
//   unknown  the radio or the image doesn't say when it was made
export function direction(probe, image) {
	if (gitSame(probe?.git, image.git)) return "same";
	const now = Number(probe?.version?.git_time) || 0, then = Number(image.git_time) || 0;
	if (!now || !then) return "unknown";
	return then > now ? "newer" : then < now ? "older" : "same";
}

// The options a manifest was built with that differ from the radio's: [[name, wanted, got]]
export function optionMismatches(manifest, chosen) {
	const out = [];
	for (const [name, value] of Object.entries(chosen || {}))
		if (manifest?.build_options?.[name] !== value) out.push([name, value, manifest?.build_options?.[name] ?? null]);
	for (const [name, value] of Object.entries(manifest?.build_options || {}))
		if (!(chosen && name in chosen)) out.push([name, null, value]);
	return out;
}

// The manifest must be the one the index promised: the same board, commit and image size
export function checkManifest(image, manifest) {
	if (manifest?.board !== image.board) throw new ReleaseError(`the manifest is for ${manifest?.board}, not ${image.board}`);
	if (image.git && manifest.git && !gitSame(image.git, manifest.git)) throw new ReleaseError(`the manifest is commit ${manifest.git}, the list says ${image.git}`);
	if (image.size && Number(manifest.image?.size) !== image.size) throw new ReleaseError("the manifest's image size differs from the list's");
}

// A release's notes as plain paragraphs and list items, for text nodes (never HTML): [{kind: "p" | "li", text}].
// HTML comments (the SPDX lines) go; Markdown emphasis and code marks are dropped
export function noteBlocks(notes) {
	const text = String(notes || "").replace(/<!--[\s\S]*?-->/g, "").replace(/\r\n?/g, "\n");
	const plain = (s) => s.replace(/\*\*|__|`/g, "").replace(/\[([^\]]+)\]\([^)]+\)/g, "$1").trim();
	const out = [];
	let para = [];
	const flush = () => { if (para.length) out.push({ kind: "p", text: plain(para.join(" ")) }); para = []; };
	for (const line of text.split("\n")) {
		const li = /^\s*[-*]\s+(.*)$/.exec(line);
		if (li) { flush(); out.push({ kind: "li", text: plain(li[1]) }); continue; }
		if (!line.trim()) { flush(); continue; }
		const head = /^#+\s+(.*)$/.exec(line);
		para.push(head ? head[1] : line.trim());
	}
	flush();
	return out.filter((b) => b.text);
}
