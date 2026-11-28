// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// A connected firmware's actual power levels, including build variants and frequency-dependent labels.

export function powerProfile(text) {
	const kv = Object.fromEntries(text.split(/\r?\n/).filter((line) => line.includes("=")).map((line) => {
		const at = line.indexOf("=");
		return [line.slice(0, at), line.slice(at + 1)];
	}));
	const count = Number(kv.power_count), defaultLevel = Number(kv.power_default);
	const names = (kv.power_base_names || "").split("/");
	if (!Number.isInteger(count) || count < 1 || count > 10 || names.length !== count || names.some((n) => !n)) return null;
	const bands = [];
	for (const entry of (kv.power_bands || "").split(",").filter(Boolean)) {
		const match = /^(\d+)-(\d+):(.+)$/.exec(entry);
		if (!match) return null;
		const lo = Number(match[1]), hi = Number(match[2]), labels = match[3].split("/");
		if (lo > hi || labels.length !== count || labels.some((n) => !n)) return null;
		bands.push({ lo, hi, names: labels });
	}
	return { names, bands, defaultLevel: Number.isInteger(defaultLevel) && defaultLevel >= 0 && defaultLevel < count ? defaultLevel : null };
}

// Match core/codeplug/codeplug.c: the stored byte is a portable ten-step scale on boards with fewer than nine levels.
export function powerByte(level, count) {
	return count < 2 || count >= 9 ? level + 1 : Math.floor((level * 9 + Math.floor((count - 1) / 2)) / (count - 1)) + 1;
}
export function powerLevel(byte, count) {
	return count >= 9 ? Math.min(byte - 1, count - 1) : Math.floor(((Math.min(byte, 10) - 1) * (count - 1) + 4) / 9);
}

export function powerNames(profile, hz) {
	return profile.bands.find((b) => hz >= b.lo && hz <= b.hi)?.names || profile.names;
}

export function radioPowerOptions(t, profile, row = {}, rows = null) {
	const count = profile.names.length;
	const names = (rows || [row]).map((r) => powerNames(profile, r.txHz));
	const label = (level) => [...new Set(names.map((n) => n[level]))].join(" / ");
	const inherited = profile.defaultLevel == null ? "" : ` (${label(profile.defaultLevel)})`;
	const opts = [[0, t("editor.power.default") + inherited]];
	for (let level = 0; level < count; level++) {
		// Keep an imported equivalent byte intact when opening or saving a form without changing Power.
		const current = !rows && Number.isInteger(row.power) && row.power > 0 && row.power <= 10 && powerLevel(row.power, count) === level;
		opts.push([current ? row.power : powerByte(level, count), label(level)]);
	}
	return opts;
}
