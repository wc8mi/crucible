// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors

import {
	AMSAT_BULLETIN, amsatEndpoint, buildTleBundle, DEFAULT_SELECTED_IDS, MAX_SELECTED_PROFILES, filterFmSatellites,
	mergeNewest, parseTleCatalog, pe0satEndpoint, SATELLITES, tleEpochMilliseconds,
} from "./satellite-tle.js";
import { pushSatelliteBundle } from "./satellite-radio.js";
import {
	Link, SerialStream, RFOUNDRY_USB, CABLES, browserErrorKey, probe, serialApi, serialSupported,
} from "./flasher/link.js";
import { page } from "./i18n.js";

const { t } = page;
const fileInput = document.getElementById("satellite-tle-file");
const latestButton = document.getElementById("tle-latest");
const options = document.getElementById("satellite-tle-options");
const buildButton = document.getElementById("satellite-tle-build");
const pushButton = document.getElementById("satellite-tle-push");
const status = document.getElementById("satellite-tle-status");
const providerInput = document.getElementById("tle-provider");
const searchInput = document.getElementById("satellite-tle-search");
const form = document.getElementById("satellite-tle");
// The online catalogs, each read through this site's own proxy. AMSAT comes first: on a tie its lines are kept, and
// its names are the ones shown.
const SOURCES = [
	{ name: "AMSAT", api: amsatEndpoint(form?.dataset.amsatApi), failed: "tools.satellite.status.fetch_failed",
		notFound: "tools.satellite.status.api_not_found" },
	{ name: "PE0SAT", api: pe0satEndpoint(form?.dataset.pe0satApi), failed: "tools.satellite.status.pe0sat_fetch_failed",
		notFound: "tools.satellite.status.pe0sat_api_not_found" },
];
const profileById = new Map(SATELLITES.map((satellite) => [satellite.id, satellite]));
const RADIO_PORT_FILTERS = [RFOUNDRY_USB, ...CABLES];
const PICKS_KEY = "satellite-tle-picks";
let catalog = null;
let live = false; // the online lists are loaded, and fetched again before a bundle is made
let sourceNotice = ""; // why only one list is in use
const selected = new Set(storedPicks() ?? DEFAULT_SELECTED_IDS);

// The boxes checked on the last visit; none stored (or no storage, as in a private window) gives the defaults
function storedPicks() {
	try {
		const ids = JSON.parse(globalThis.localStorage?.getItem(PICKS_KEY) ?? "null");
		return Array.isArray(ids) ? ids.filter((id) => profileById.has(id)).slice(0, MAX_SELECTED_PROFILES) : null;
	} catch {
		return null;
	}
}

function storePicks() {
	try {
		globalThis.localStorage?.setItem(PICKS_KEY, JSON.stringify(selectedIds()));
	} catch {
		// private window or storage turned off: the picks last for this visit only
	}
}

// A bundle names one HTTPS provider. Merged lists name this page, which put them together; a plain HTTP preview names
// AMSAT so the bundle check still passes there.
function pageProvider() {
	try {
		const url = new URL(globalThis.location.href);
		url.search = "";
		url.hash = "";
		return url.protocol === "https:" ? url.href : AMSAT_BULLETIN;
	} catch {
		return AMSAT_BULLETIN;
	}
}

function selectedIds() {
	return [...selected];
}

function profileValues(selected) {
	return selected.map((id) => {
		const satellite = profileById.get(id);
		if (!satellite) throw new Error(`NORAD ${id}: no FM frequency profile is available`);
		return { catalog_id: id, rx_hz: satellite.rxHz, tx_hz: satellite.txHz, tx_tone_tenths: satellite.toneHz };
	});
}

function makeBundle(source = catalog) {
	const provider = new URL(providerInput.value.trim());
	if (provider.protocol !== "https:" || provider.username || provider.password || provider.hash)
		throw new Error("Catalog URL must use HTTPS and contain no credentials or fragment");
	return buildTleBundle(source, selectedIds(), { provider: provider.href, profiles: profileValues(selectedIds()) });
}

function showReadyState() {
	const selected = selectedIds();
	buildButton.disabled = true;
	pushButton.disabled = true;
	if (!catalog) {
		status.textContent = t("tools.satellite.status.pick");
		return;
	}
	if (!selected.length) {
		status.textContent = t("tools.satellite.status.none");
		return;
	}
	if (selected.length > MAX_SELECTED_PROFILES) {
		status.textContent = t("tools.satellite.status.limit");
		return;
	}
	try {
		makeBundle();
		buildButton.disabled = false;
		pushButton.disabled = false;
		status.textContent = t("tools.satellite.status.ready", { n: selected.length }) +
			(sourceNotice ? ` ${sourceNotice}` : "");
	} catch (error) {
		status.textContent = t("tools.satellite.status.invalid", { error: error.message });
	}
}

function ageDays(record) {
	try {
		return (Date.now() - tleEpochMilliseconds(record.line1, record.catalog_id)) / 86400000;
	} catch {
		return null;
	}
}

// The list's name when it holds the page's own ("ISS (ZARYA)"), else both: PE0SAT, for one, calls IO-86 LAPAN-A2,
// and a box or search under that name alone would hide it
function profileLabel(record) {
	const own = profileById.get(record.catalog_id)?.name || record.name;
	if (!record.name || record.name.toLocaleLowerCase().includes(own.toLocaleLowerCase())) return record.name || own;
	return `${own} (${record.name})`;
}

function renderOptions() {
	const records = filterFmSatellites(catalog || SATELLITES.map((satellite) => ({
		catalog_id: satellite.id, name: satellite.name,
	})));
	const query = searchInput.value.trim().toLocaleLowerCase();
	const matching = records.filter((record) => `${profileLabel(record)} ${record.catalog_id}`.toLocaleLowerCase()
		.includes(query));
	for (const record of matching) {
		const satellite = profileById.get(record.catalog_id);
		const row = document.createElement("div");
		row.className = satellite.txHz ? "tle-profile" : "tle-profile tle-profile-listen";
		row.dataset.id = String(record.catalog_id);
		const checkbox = document.createElement("input");
		checkbox.type = "checkbox";
		checkbox.value = String(record.catalog_id);
		checkbox.id = `tle-satellite-${record.catalog_id}`;
		checkbox.checked = selected.has(record.catalog_id);
		checkbox.addEventListener("change", () => {
			if (checkbox.checked && selected.size >= MAX_SELECTED_PROFILES) {
				checkbox.checked = false;
				status.textContent = t("tools.satellite.status.limit");
				return;
			}
			if (checkbox.checked) selected.add(record.catalog_id);
			else selected.delete(record.catalog_id);
			storePicks();
			showReadyState();
		});
		const name = document.createElement("label");
		name.className = "tle-profile-name";
		name.htmlFor = checkbox.id;
		name.textContent = profileLabel(record);
		const detail = document.createElement("small");
		detail.className = "tle-profile-detail";
		const rx = (satellite.rxHz / 1e6).toFixed(3);
		detail.textContent = satellite.txHz ?
			t("tools.satellite.option", { id: record.catalog_id, rx, tx: (satellite.txHz / 1e6).toFixed(3) }) :
			t("tools.satellite.option_listen", { id: record.catalog_id, rx });
		const age = record.line1 ? ageDays(record) : null;
		if (age !== null)
			detail.textContent += ` · ${t("tools.satellite.option_age", { age: age < 1 ? "<1" : Math.round(age),
				source: record.source || t("tools.satellite.file") })}`;
		row.append(checkbox, name, detail);
		options.append(row);
	}
	if (!matching.length) {
		const empty = document.createElement("p");
		empty.className = "small muted";
		empty.textContent = t("tools.satellite.no_matches");
		options.append(empty);
	}
}

function loadCatalog(records, provider, online) {
	catalog = records;
	live = online;
	providerInput.value = provider;
	const available = new Set(filterFmSatellites(catalog).map((record) => record.catalog_id));
	for (const id of selected) if (!available.has(id)) selected.delete(id);
	storePicks();
	options.replaceChildren();
	renderOptions();
	showReadyState();
}

async function fetchSourceText(source) {
	let response;
	try {
		response = await fetch(source.api, { cache: "no-store", headers: { Accept: "text/plain" } });
	} catch (error) {
		throw new Error(t(source.failed, { status: error.message }));
	}
	if (!response.ok)
		throw new Error(response.status === 404 ? t(source.notFound) : t(source.failed, { status: response.status }));
	const text = await response.text();
	if (new TextEncoder().encode(text).length > 1000000)
		throw new Error(t("tools.satellite.status.too_large"));
	return text;
}

// Both lists, merged newest-first per satellite. One that fails is left out and named in sourceNotice; both failing
// is an error with both reasons.
async function fetchLatest() {
	const results = await Promise.allSettled(SOURCES.map(async (source) =>
		({ source: source.name, records: parseTleCatalog(await fetchSourceText(source)) })));
	const loaded = results.filter((result) => result.status === "fulfilled").map((result) => result.value);
	const failures = results.map((result, i) => result.status === "rejected" ?
		{ source: SOURCES[i].name, error: result.reason.message } : null).filter(Boolean);
	if (!loaded.length) throw new Error(failures.map((failure) => failure.error).join(" "));
	sourceNotice = failures.length ? t("tools.satellite.status.latest_one",
		{ source: loaded.map((list) => list.source).join(", "), error: failures.map((f) => f.error).join(" ") }) : "";
	return mergeNewest(loaded);
}

async function refreshSource() {
	if (!live) return;
	status.textContent = t("tools.satellite.status.latest_fetching");
	const refreshed = await fetchLatest();
	makeBundle(refreshed);
	catalog = refreshed;
}

function setPushProgress(step) {
	status.textContent = t(`tools.satellite.status.radio_${step}`);
}

function setBusy(busy) {
	for (const control of [latestButton, fileInput, providerInput, searchInput, buildButton, pushButton,
		...options.querySelectorAll("input")])
		control.disabled = busy;
}

latestButton.addEventListener("click", async () => {
	setBusy(true);
	status.textContent = t("tools.satellite.status.latest_fetching");
	try {
		loadCatalog(await fetchLatest(), pageProvider(), true);
	} catch (error) {
		catalog = null;
		buildButton.disabled = true;
		pushButton.disabled = true;
		status.textContent = error.message;
	} finally {
		setBusy(false);
		const result = status.textContent;
		showReadyState();
		status.textContent = result;
	}
});

fileInput.addEventListener("change", async () => {
	const file = fileInput.files?.[0];
	if (!file) return;
	if (file.size > 1000000) {
		catalog = null;
		buildButton.disabled = true;
		pushButton.disabled = true;
		status.textContent = t("tools.satellite.status.too_large");
		return;
	}
	try {
		sourceNotice = "";
		loadCatalog(parseTleCatalog(await file.text()), providerInput.value.trim() || AMSAT_BULLETIN, false);
	} catch (error) {
		catalog = null;
		buildButton.disabled = true;
		pushButton.disabled = true;
		status.textContent = t("tools.satellite.status.read_failed", { error: error.message });
	}
});

providerInput.addEventListener("input", showReadyState);

buildButton.addEventListener("click", async () => {
	setBusy(true);
	try {
		await refreshSource();
		const bundle = makeBundle();
		const filename = `satellites-${new Date().toISOString().slice(0, 10)}.json`;
		const url = URL.createObjectURL(new Blob([`${JSON.stringify(bundle, null, 2)}\n`], { type: "application/json" }));
		const link = Object.assign(document.createElement("a"), { href: url, download: filename });
		link.click();
		setTimeout(() => URL.revokeObjectURL(url), 10000);
		status.textContent = t("tools.satellite.status.done", { name: filename });
	} catch (error) {
		status.textContent = t("tools.satellite.status.invalid", { error: error.message });
	} finally {
		setBusy(false);
		const result = status.textContent;
		showReadyState();
		status.textContent = result;
	}
});

renderOptions();

pushButton.addEventListener("click", async () => {
	let stream;
	let succeeded = false;
	setBusy(true);
	try {
		setPushProgress("connecting");
		if (!serialSupported()) throw new Error(t("flash.err.no_serial"));
		const port = await serialApi().requestPort({ filters: RADIO_PORT_FILTERS });
		await refreshSource();
		const bundle = makeBundle();
		stream = await SerialStream.open(port);
		const link = new Link(stream);
		setPushProgress("checking");
		if (!await probe(link)) throw new Error(t("tools.satellite.status.radio_not_found"));
		await pushSatelliteBundle(link, bundle, setPushProgress);
		succeeded = true;
	} catch (error) {
		const browserError = browserErrorKey(error);
		status.textContent = t("tools.satellite.status.push_failed", {
			error: error.translationKey ? t(error.translationKey) : browserError ? t(browserError) : error.message,
		});
	} finally {
		if (stream) await stream.close();
		setBusy(false);
		if (succeeded) {
			buildButton.disabled = false;
			pushButton.disabled = false;
		} else {
			const failure = status.textContent;
			showReadyState();
			status.textContent = failure;
		}
	}
});
showReadyState();
searchInput.addEventListener("input", () => {
	options.replaceChildren();
	renderOptions();
	showReadyState();
});
