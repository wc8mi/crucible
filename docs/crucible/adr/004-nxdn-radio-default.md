<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 David Behnke -->

# 004. NXDN radio default: stored, 4800 bit/s, a quick rate switch

| | |
|---|---|
| Status | Proposed |
| Date | 2026-11-27 |
| Decides | Where the VFO's NXDN settings come from, the default rate, and how Talk to works in NXDN |

## Context

NXDN runs on one of two sets of settings ([nxdn.md](../../spec/nxdn.md) 6.2):

- **A stored NXDN channel** runs on its own record: the rate, both RANs, the squelch and the talk target.
- **The VFO in NXDN, and a channel in NXDN for now,** run on the radio's NXDN settings. Before this record they were
  RAM only: 9600 bit/s, RAN 1, group 1 after each restart. Only the debug link could change them.

Owners reported four problems:

1. In the NXDN VFO, a talk group change does not apply. Talk to went to the DMR path only.
2. There is no NXDN setting in the menus that stays after a restart.
3. Most NXDN users run 4800 bit/s. 9600 was the default.
4. Changing the rate takes too many steps.

## Decision

1. **A stored NXDN radio default.** Four values in Settings > Talk & listen > NXDN: Rate (4800, 9600 bit/s), RX RAN
   (0 to 63, 0 = any), TX RAN (0 to 63) and Talkgroup (1 to 65519, or All). They seed the radio's NXDN settings at
   start-up. A change in Settings is stored and applies at once. The unit ID, emergency, group squelch and unit call
   stay RAM only (the debug link).
2. **4800 bit/s is the default.** A new radio: 4800 bit/s, any RAN in, RAN 1 out, talkgroup 1. A new NXDN channel in
   the radio's channel editor takes the radio default's rate and RANs. The web codeplug editor's new NXDN channel is
   4800 bit/s too. Stored channels keep their own rate.
3. **Talk to works on the radio default.** In the VFO and on a channel in NXDN for now:
   - Talk to (OK) sets the target for now. Back goes back to the stored talkgroup.
   - Keep (hold OK) on a group stores it as the talkgroup. A unit is always for now.
   - DMR's all call is NXDN's All (0xFFFF). ID 0 and IDs above 65519 are refused: "NXDN IDs go up to 65519".
   - On a stored NXDN channel, Talk to is refused: "NXDN: set in the channel".
4. **A quick rate switch.** In NXDN, quick tile 8 (Width in FM) is the rate. OK flips the stored rate between 4800 and
   9600. A rate the board's provider cannot run is refused. On a stored NXDN channel the tile shows the channel's
   rate dimmed, and OK says "NXDN: set in the channel".
5. **No seed after start-up.** A reload (any settings change) or the end of a debug link detour does not seed the
   settings again. A target set for now, or debug link values, stay until the next restart or a change in Settings.

## Options considered

| Option | Why not |
|---|---|
| Store every NXDN setting (unit ID, squelch, emergency) | The unit ID already has a radio-wide codeplug record. The others are test settings. |
| A temporary talk target as a new view flag | No free bit in the view's NXDN flags. The UI finds it: the target differs from the stored talkgroup. |
| Seed again on every reload | A reload follows unrelated settings changes. It would drop a target set for now. |
| Write Talk to into a stored NXDN channel's record | A codeplug write for each target change. Left for later. |

## Consequences

- **Settings record.** A second Crucible block of eight bytes is appended (settings_t 428 to 436 bytes, still two
  pages): a marker (0xA7, offset 428), flags (bit 0: 9600 bit/s), RX RAN, TX RAN, the talkgroup (u16, offset 432) and
  two reserved bytes. A record shorter than 436 bytes, or with another marker, reads as the defaults. Values out of
  range read as their default; All (0xFFFF) is kept. No save is forced.
- **Upstream collision.** As for ADR 003's block: if RadioFoundry appends its own field at byte 428, a Crucible radio
  sees a wrong marker and uses the defaults. There is a 1 in 256 chance that a foreign byte equals 0xA7, so its other
  bytes are read as NXDN values (then clamped to range). Upstream fields taken into Crucible later go after
  Crucible's blocks, never between them. A user who installs RadioFoundry after Crucible should reset the settings.
- **Existing radios.** After the update, the VFO's NXDN starts at 4800 bit/s, any RAN in, RAN 1 out, talkgroup 1.
  Stored NXDN channels do not change.
- **Debug link.** `nxdn.default.rate rx_ran tx_ran tg` show the stored values; `nxdn.rate` and the others still show
  the RAM settings. Op 43 changes RAM only. A change in Settings and op 43 at the same time can lose one of them.
- **Scope.** P25 in the VFO has the same talk target gap. It is not changed here.

## Plan

| Step | Work | Needs a radio |
|---|---|---|
| 1 | Settings block, start-up seed, Talk to, quick tile, Settings page, tests, docs | No |
| 2 | A test release | No |
| 3 | An on-air check at both rates on each NXDN radio | Yes |

## Review log

| Round | Reviewer | Result |
|---|---|---|
| 1 | Independent (code) | Revise: Talk to paths, ID mapping, start-up seed in the radio core, settings block, UI plumbing, scan and dual watch scope, tests, web editor default |
| 2 | Independent (code) | Revise: no free view flag bit, temporary target found in the UI, no seed on reload or detour, All group ID, the UI simulator, more tests, quick tile refusals |
| 3 | Independent (code) | Accept. Notes: Up past 65519 is All, typed values above 65519 refused; the block check keeps 0xFFFF; a talk target of 0 in the settings becomes group 1; nxdn.md notes a failed tag read; the help line says stored NXDN channels do not change |
