// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke

// PE0SAT's amateur TLE list. PE0SAT serves plain HTTP only, so the page reads it here, over this site's HTTPS.
const CATALOG_URL = "http://tle.pe0sat.nl/kepler/amateur.txt";
const MAX_BYTES = 1000000;

export async function onRequestGet() {
	try {
		const upstream = await fetch(CATALOG_URL, {
			cache: "no-store",
			headers: { Accept: "text/plain", "User-Agent": "RadioFoundry site TLE updater" },
		});
		if (!upstream.ok) return new Response("PE0SAT list is temporarily unavailable", { status: 502 });
		const body = await upstream.arrayBuffer();
		if (body.byteLength > MAX_BYTES) return new Response("PE0SAT list exceeds the size limit", { status: 502 });
		return new Response(body, {
			headers: {
				"Content-Type": "text/plain; charset=utf-8",
				"Cache-Control": "no-store",
				"X-Content-Type-Options": "nosniff",
			},
		});
	} catch {
		return new Response("PE0SAT list is temporarily unavailable", { status: 502 });
	}
}
