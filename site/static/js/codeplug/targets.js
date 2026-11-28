// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The radios a codeplug is made for, without one on the line. Every radio stores the same codeplug, so one file serves
// them all; what differs is what each image can do with a channel: its power levels, the frequencies it transmits and
// receives on, and the channel modes its firmware has. A target is one image of a radio (radios.json codeplug.profiles,
// from tools/boards.py radio_profiles, and the build's features, config.full). The power byte stays the portable
// ten-step scale (power.js): a target only says what that byte becomes on its radio, it never rewrites the byte.

import { powerLevel, powerNames } from "./power.js";
import { kindRow } from "./modes.js";

// One target per image of each radio that has a codeplug and a profile, in radios.json order. key: the radio id, or
// "<id>:<build>" for a build option's other images
export function radioTargets(radios) {
	const out = [];
	for (const r of radios || []) {
		const profiles = r.codeplug?.profiles || [];
		profiles.forEach((p, i) => {
			if (!p.names?.length) return;
			out.push({
				key: i === 0 ? r.id : `${r.id}:${p.build}`,
				radio: r.id,
				name: profiles.length > 1 && p.title ? `${r.name} ${p.title}` : r.name,
				boardName: p.board_name,
				// no default level: offline, the radio's own default (the user's power setting) is unknown;
				// factoryLevel is the level a radio starts with
				power: { names: p.names, bands: p.bands || [], defaultLevel: null },
				factoryLevel: Number.isInteger(p.default) ? p.default : null,
				ranges: p.ranges || [],
				features: r.config?.full || null,
			});
		});
	}
	return out;
}

// The target of the radio on the line: by the board name it reports (debug link board=), else the radio's first image
// (older firmware reports an alias, radios.json codeplug.names)
export function targetFor(targets, boardName, radioId = "") {
	const b = String(boardName || "").toLowerCase();
	return targets.find((x) => String(x.boardName).toLowerCase() === b) || (radioId && targets.find((x) => x.radio === radioId)) || null;
}

// What a channel's power byte means on a target: the level's label in the channel's TX band. Byte 0 is the radio's own
// default, which only the radio knows: level and label null, factory the label of the level it starts with (or null).
// {level, label, isDefault, factory}
export function powerOn(target, byte, hz) {
	const p = target.power, names = powerNames(p, hz);
	if (!byte) return { level: null, label: null, isDefault: true, factory: target.factoryLevel == null ? null : names[target.factoryLevel] };
	const level = powerLevel(byte, p.names.length);
	return { level, label: names[level], isDefault: false, factory: null };
}

// One channel's power on every target: [{target, level, label, isDefault}]
export function powerAcross(targets, byte, hz) {
	return targets.map((target) => ({ target, ...powerOn(target, byte, hz) }));
}

// a range's fourth item marks one only a feature switch or a setting adds (receive past the rated bands)
const inRange = (ranges, hz, tx, opt = true) => ranges.some(([lo, hi, canTx, extra]) => hz >= lo && hz <= hi && (!tx || canTx) && (opt || !extra));

// Why a channel won't work as stored on a target: codes "target_rx" (the radio can't receive the RX frequency),
// "target_rx_ext" (only with receive past the rated bands turned on), "target_tx" (it can't transmit on the TX
// frequency), "target_mode" (its firmware lacks the channel's mode). Empty when the channel works there
export function channelOn(target, c) {
	const out = [];
	if (target.ranges.length && c.rxHz > 0 && !inRange(target.ranges, c.rxHz, false, false)) {
		out.push({ code: inRange(target.ranges, c.rxHz, false) ? "target_rx_ext" : "target_rx", field: "rxHz" });
	}
	if (target.ranges.length && !c.rxOnly && c.txHz > 0 && !inRange(target.ranges, c.txHz, true)) out.push({ code: "target_tx", field: "txHz" });
	if (target.features) {
		const need = c.kind ? kindRow(c.kind)?.feature : c.mode === "dmr" ? "DMR" : null;
		if (need && !target.features.includes(need)) {
			out.push({ code: "target_mode", field: "mode", mode: c.kind ? kindRow(c.kind)?.short : "DMR" });
		}
	}
	return out;
}

// The Problems tab's warnings for a codeplug made for these targets: one per channel and reason, naming every target
// it fails on, so five radios don't make five rows of the same thing; allName (when given) names them when it fails on
// every one of several. Same shape as model.js validate()
export function targetProblems(m, targets, allName = "") {
	const out = [];
	if (!targets?.length) return out;
	for (const c of m.channels) {
		const by = new Map();
		for (const target of targets) {
			for (const x of channelOn(target, c)) {
				const k = `${x.code}:${x.mode || ""}`;
				if (!by.has(k)) by.set(k, { ...x, names: [] });
				by.get(k).names.push(target.name);
			}
		}
		for (const x of by.values()) {
			out.push({ level: "warning", table: "channels", item: c, field: x.field, code: x.code,
				params: { radios: allName && targets.length > 1 && x.names.length === targets.length ? allName : x.names.join(", "), mode: x.mode || "" } });
		}
	}
	return out;
}
