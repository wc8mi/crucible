// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors

// The firmware's built-in profiles, in its order (core/satellite/frequency.c). txHz 0 is listen only: SSTV and
// telemetry downlinks, and satellites whose uplink the radio can't use; the radio refuses PTT while tracking one.
export const SATELLITES = Object.freeze([
	{ id: 25544, name: "ISS", rxHz: 437800000, txHz: 145990000, toneHz: 670 },
	{ id: 27607, name: "SO-50", rxHz: 436795000, txHz: 145850000, toneHz: 670 },
	{ id: 39444, name: "AO-73", rxHz: 145935000, txHz: 0, toneHz: 0 },
	{ id: 40908, name: "LILACSAT-2", rxHz: 437200000, txHz: 144350000, toneHz: 0 },
	{ id: 40931, name: "IO-86", rxHz: 435880000, txHz: 145880000, toneHz: 885 },
	{ id: 43017, name: "AO-91", rxHz: 145960000, txHz: 435250000, toneHz: 670 },
	{ id: 43678, name: "PO-101", rxHz: 145900000, txHz: 437500000, toneHz: 1413 },
	{ id: 43770, name: "AO-95", rxHz: 145920000, txHz: 0, toneHz: 0 },
	{ id: 48274, name: "CSS", rxHz: 436510000, txHz: 145875000, toneHz: 0 },
	{ id: 57172, name: "UMKA-1", rxHz: 437625000, txHz: 0, toneHz: 0 },
	{ id: 57180, name: "MONITOR-3", rxHz: 435290000, txHz: 0, toneHz: 0 },
	{ id: 57182, name: "MONITOR-4", rxHz: 436080000, txHz: 0, toneHz: 0 },
	{ id: 57189, name: "VIZARD-METEO", rxHz: 437825000, txHz: 0, toneHz: 0 },
	{ id: 57203, name: "UTMN-2", rxHz: 436125000, txHz: 0, toneHz: 0 },
	{ id: 59112, name: "SONATE-2", rxHz: 145880000, txHz: 0, toneHz: 0 },
	{ id: 61781, name: "AO-123", rxHz: 435400000, txHz: 145850000, toneHz: 670 },
	{ id: 63213, name: "TEVEL2-4", rxHz: 436400000, txHz: 145970000, toneHz: 0 },
	{ id: 63214, name: "TEVEL2-5", rxHz: 436400000, txHz: 145970000, toneHz: 0 },
	{ id: 63215, name: "TEVEL2-6", rxHz: 436400000, txHz: 145970000, toneHz: 0 },
	{ id: 63217, name: "TEVEL2-1", rxHz: 436400000, txHz: 145970000, toneHz: 0 },
	{ id: 63218, name: "TEVEL2-3", rxHz: 436400000, txHz: 145970000, toneHz: 0 },
	{ id: 63219, name: "TEVEL2-2", rxHz: 436400000, txHz: 145970000, toneHz: 0 },
	{ id: 63237, name: "TEVEL2-9", rxHz: 436400000, txHz: 145970000, toneHz: 0 },
	{ id: 63238, name: "TEVEL2-7", rxHz: 436400000, txHz: 145970000, toneHz: 0 },
	{ id: 63239, name: "TEVEL2-8", rxHz: 436400000, txHz: 145970000, toneHz: 0 },
	{ id: 67287, name: "LUCA-1", rxHz: 437180000, txHz: 0, toneHz: 0 },
	{ id: 67291, name: "RS95S", rxHz: 436950000, txHz: 145920000, toneHz: 670 },
	{ id: 101101, name: "SO-131", rxHz: 436888000, txHz: 145925000, toneHz: 0 },
	{ id: 101102, name: "SO-132", rxHz: 436665000, txHz: 0, toneHz: 0 },
].map(Object.freeze));

export const DEFAULT_SELECTED_IDS = Object.freeze([25544, 27607, 43017]);
export const AMSAT_BULLETIN = "https://www.amsat.org/tle/daily-bulletin.txt";
// PE0SAT's amateur list serves plain HTTP only; the page reads it through PE0SAT_API, over this site's HTTPS
export const PE0SAT_CATALOG = "http://tle.pe0sat.nl/kepler/amateur.txt";
export const MAX_SELECTED_PROFILES = 10;
// The same-origin proxies for AMSAT_BULLETIN and PE0SAT_CATALOG (functions/api/*-tle.js). A page served from another
// path names its own in data-amsat-api and data-pe0sat-api; only a same-origin path is taken, so a page cannot point a
// button at another site.
export const AMSAT_API = "/api/amsat-tle";
export const PE0SAT_API = "/api/pe0sat-tle";

export function sameOriginEndpoint(value, fallback, base = globalThis.location?.href) {
	const v = typeof value === "string" ? value.trim() : "";
	if (!v || !base) return fallback;
	try {
		// the browser's own parse of the very string returned, so a tab, newline, backslash or a space that trim()
		// strips cannot turn the path into another host
		return new URL(v, base).origin === new URL(base).origin ? v : fallback;
	} catch {
		return fallback;
	}
}

export function amsatEndpoint(value, base = globalThis.location?.href) {
	return sameOriginEndpoint(value, AMSAT_API, base);
}

export function pe0satEndpoint(value, base = globalThis.location?.href) {
	return sameOriginEndpoint(value, PE0SAT_API, base);
}

// One catalog from several: for each catalog ID, the lines with the newest epoch. The name stays the one from the first
// catalog that has the ID (AMSAT's names are the familiar ones), and each record notes the source its lines came from.
export function mergeNewest(catalogs) {
	const byId = new Map();
	const epoch = (record) => {
		try {
			return tleEpochMilliseconds(record.line1, record.catalog_id);
		} catch {
			return -Infinity;
		}
	};
	for (const { source, records } of catalogs) {
		for (const record of records) {
			const earlier = byId.get(record.catalog_id);
			if (!earlier) byId.set(record.catalog_id, { ...record, source });
			else if (epoch(record) > epoch(earlier))
				byId.set(record.catalog_id, { ...earlier, line1: record.line1, line2: record.line2, source });
		}
	}
	return [...byId.values()];
}

export function filterFmSatellites(records) {
	const fmIds = new Set(SATELLITES.map((satellite) => satellite.id));
	return records.filter((record) => fmIds.has(record.catalog_id));
}

// Six-digit catalog numbers need satellite protocol 8 on the radio, as listen-only profiles do
export const CATALOG_ID_MAX = 339999;
const ALPHA5_LETTERS = "ABCDEFGHJKLMNPQRSTUVWXYZ";	// 10..33: Alpha-5 skips I and O

// Columns 3-7 of a TLE line: five digits, or Alpha-5 (a letter for the leading two digits) past 99999
export function tleCatalogId(field) {
	if (/^\d{5}$/.test(field)) return Number(field);
	if (/^[A-HJ-NP-Z]\d{4}$/.test(field)) return (10 + ALPHA5_LETTERS.indexOf(field[0])) * 10000 + Number(field.slice(1));
	return null;
}

export function tleCatalogField(id) {
	return id < 100000 ? String(id).padStart(5, "0") :
		ALPHA5_LETTERS[Math.floor(id / 10000) - 10] + String(id % 10000).padStart(4, "0");
}

const DAY_MS = 86400000;
const MAX_AGE_DAYS = 30;

export function parseTleCatalog(text) {
	if (typeof text !== "string") throw new Error("TLE catalog must be text");
	const lines = text.replace(/^\uFEFF/, "").split(/\r?\n/);
	const records = [];
	// Published lists can repeat an ID: two objects sharing one element set (the same lines under two names), or two
	// element sets for one object. A repeat with the same lines keeps the first; with other lines the page cannot tell
	// which is right, so that ID is left out (a selected one is then reported missing) instead of the whole list.
	const seen = new Map();
	const conflicting = new Set();
	let name = "";
	for (let i = 0; i < lines.length;) {
		const line = lines[i];
		if (!line.trim()) {
			i++;
			continue;
		}
		if (line.startsWith("2 ")) throw new Error("Catalog contains an unpaired TLE line 2");
		if (!line.startsWith("1 ")) {
			name = (line.startsWith("0 ") ? line.slice(2) : line).trim();
			i++;
			continue;
		}
		let line2Index = i + 1;
		while (line2Index < lines.length && !lines[line2Index].trim()) line2Index++;
		const line2 = lines[line2Index];
		if (!line2?.startsWith("2 ")) throw new Error("Catalog contains an incomplete TLE pair");
		const catalogId = tleCatalogId(line.slice(2, 7));
		if (catalogId === null) throw new Error("Catalog contains an invalid NORAD catalog ID");
		if (!name) throw new Error(`Catalog ID ${catalogId} has no satellite name`);
		const earlier = seen.get(catalogId);
		if (!earlier) {
			const record = { catalog_id: catalogId, name, line1: line, line2 };
			records.push(record);
			seen.set(catalogId, record);
		} else if (earlier.line1 !== line || earlier.line2 !== line2) {
			conflicting.add(catalogId);
		}
		name = "";
		i = line2Index + 1;
	}
	const kept = records.filter((record) => !conflicting.has(record.catalog_id));
	if (!kept.length) throw new Error("No TLE records found in the selected file");
	return kept;
}

function checksumValid(line) {
	if (line.length !== 69 || !/^\d$/.test(line[68])) return false;
	let sum = 0;
	for (const char of line.slice(0, 68)) {
		if (char >= "0" && char <= "9") sum += Number(char);
		else if (char === "-") sum++;
	}
	return sum % 10 === Number(line[68]);
}

export function tleEpochMilliseconds(line1, id) {
	const match = /^(\d{2})(\d{3})\.(\d{8})$/.exec(line1.slice(18, 32));
	if (!match) throw new Error(`NORAD ${id}: invalid TLE epoch`);
	const shortYear = Number(match[1]);
	const year = shortYear >= 57 ? 1900 + shortYear : 2000 + shortYear;
	const dayOfYear = Number(match[2]);
	const leap = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
	if (dayOfYear < 1 || dayOfYear > (leap ? 366 : 365)) throw new Error(`NORAD ${id}: invalid TLE epoch day`);
	return Date.UTC(year, 0, 1) + (dayOfYear - 1 + Number(`0.${match[3]}`)) * DAY_MS;
}

function validateRecord(record, now) {
	const { catalog_id: id, name, line1, line2 } = record;
	if (!Number.isInteger(id) || id < 1 || id > CATALOG_ID_MAX || !name || /[\x00-\x1f\x7f]/.test(name) ||
		new TextEncoder().encode(name).length > 48) throw new Error("Invalid satellite ID or name");
	if (line1.length !== 69 || line2.length !== 69 || !line1.startsWith("1 ") || !line2.startsWith("2 ") ||
		!checksumValid(line1) || !checksumValid(line2)) throw new Error(`NORAD ${id}: invalid TLE length or checksum`);
	if (tleCatalogId(line1.slice(2, 7)) !== id || tleCatalogId(line2.slice(2, 7)) !== id)
		throw new Error(`NORAD ${id}: TLE catalog IDs do not match`);
	if (!"UCS".includes(line1[7]) || line1[8] !== " " || line1[17] !== " " || line1[32] !== " " ||
		!/^[ 0-9A-Za-z]{8}$/.test(line1.slice(9, 17)) || !/^[ +-]\.\d{8}$/.test(line1.slice(33, 43)) ||
		!/^[ +-]\d{5}[+-]\d$/.test(line1.slice(44, 52)) || !/^[ +-]\d{5}[+-]\d$/.test(line1.slice(53, 61)) ||
		line1[43] !== " " || line1[52] !== " " || line1[61] !== " " || !/^\d$/.test(line1[62]) ||
		line1[63] !== " " || !/^\d{1,4}$/.test(line1.slice(64, 68).trim()))
		throw new Error(`NORAD ${id}: malformed TLE line 1`);
	if ([7, 16, 25, 33, 42, 51].some((index) => line2[index] !== " ") ||
		!/^(?: *\d{1,3}\.\d{4})$/.test(line2.slice(8, 16)) ||
		!/^(?: *\d{1,3}\.\d{4})$/.test(line2.slice(17, 25)) || !/^\d{7}$/.test(line2.slice(26, 33)) ||
		!/^(?: *\d{1,3}\.\d{4})$/.test(line2.slice(34, 42)) ||
		!/^(?: *\d{1,3}\.\d{4})$/.test(line2.slice(43, 51)) ||
		!/^(?:\d{2}| \d)\.\d{8}$/.test(line2.slice(52, 63)) || !/^ *\d{1,5}$/.test(line2.slice(63, 68)))
		throw new Error(`NORAD ${id}: malformed TLE line 2`);
	const epochMs = tleEpochMilliseconds(line1, id);
	const ageDays = (now - epochMs) / DAY_MS;
	if (ageDays < -1 || ageDays > MAX_AGE_DAYS) throw new Error(`NORAD ${id}: TLE is outside the 30-day age limit`);
	const angles = [line2.slice(8, 16), line2.slice(17, 25), line2.slice(34, 42), line2.slice(43, 51)].map(Number);
	const eccentricity = Number(`0.${line2.slice(26, 33)}`);
	const meanMotion = Number(line2.slice(52, 63));
	if (angles.some((value, i) => !Number.isFinite(value) || value < 0 || value >= (i === 0 ? 180 : 360)) ||
		eccentricity < 0 || eccentricity >= 1 || !Number.isFinite(meanMotion) || meanMotion <= 0)
		throw new Error(`NORAD ${id}: orbital value is outside its valid range`);
}

export function buildTleBundle(catalog, selectedIds,
	{ now = new Date(), provider = AMSAT_BULLETIN, profiles: suppliedProfiles = [] } = {}) {
	const selected = [...new Set(selectedIds)].sort((a, b) => a - b);
	if (!selected.length) throw new Error("Select at least one satellite");
	if (selected.length > MAX_SELECTED_PROFILES) throw new Error(`Select no more than ${MAX_SELECTED_PROFILES} satellites`);
	const byId = new Map(catalog.map((record) => [record.catalog_id, record]));
	const missing = selected.filter((id) => !byId.has(id));
	if (missing.length) throw new Error(`Catalog is missing selected IDs: ${missing.join(", ")}`);
	const catalogProfiles = new Map(SATELLITES.map((satellite) => [satellite.id, {
	catalog_id: satellite.id, rx_hz: satellite.rxHz, tx_hz: satellite.txHz, tx_tone_tenths: satellite.toneHz,
	}]));
	const profileOverrides = new Map(suppliedProfiles.map((profile) => [profile.catalog_id, profile]));
	const time = now instanceof Date ? now.getTime() : Number(now);
	if (!Number.isFinite(time)) throw new Error("Invalid bundle timestamp");
	const records = selected.map((id) => {
		const record = byId.get(id);
		validateRecord(record, time);
		return { catalog_id: record.catalog_id, name: record.name, line1: record.line1, line2: record.line2 };
	});
	const profiles = selected.map((id) => {
		const profile = profileOverrides.get(id) || catalogProfiles.get(id);
		if (!profile || profile.catalog_id !== id || !Number.isInteger(profile.rx_hz) || profile.rx_hz <= 0 ||
		    profile.rx_hz >= 1000000000 || !Number.isInteger(profile.tx_hz) || profile.tx_hz < 0 ||
		    profile.tx_hz >= 1000000000 || !Number.isInteger(profile.tx_tone_tenths) ||
		    profile.tx_tone_tenths < 0 ||
		    (profile.tx_tone_tenths !== 0 && (profile.tx_hz === 0 || profile.tx_tone_tenths < 600 ||
		     profile.tx_tone_tenths > 2600)))
			throw new Error(`NORAD ${id}: enter valid RX, TX, and tone frequencies`);
		return { catalog_id: id, rx_hz: profile.rx_hz, tx_hz: profile.tx_hz,
			tx_tone_tenths: profile.tx_tone_tenths };
	});
	return {
		schema_version: 3,
		provider,
		fetched_at: new Date(time).toISOString().replace(/\.\d{3}Z$/, "Z"),
		profile_ids: selected,
		profiles,
		records,
	};
}