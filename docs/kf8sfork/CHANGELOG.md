<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 David Behnke -->

# KF8S Fork Changelog

Changes added to `kf8sfork`, now Crucible, from its first feature commit on November 23, 2026, through
v0.1.0 on November 27, 2026.
This lists fork-specific changes, not the features inherited from upstream RadioFoundry.
See the [usage and testing guide](README.md) for menu paths and test instructions.

## v0.1.0, November 27, 2026

The first Crucible version. Versions are plain semantic versions: a fix raises the patch (0.1.1), new features the
minor (0.2.0). Every release is a test release (a GitHub pre-release) until a build is stable. The radio shows the
version on the boot screen and in About. Earlier test builds had dated names
(`crucible-<upstream>-<commit>-<yymmdd>-<n>`), and one was published as `v0.1.0-beta.1`; their GitHub releases are
removed and their tags kept.

Changes since the last dated test build (`crucible-4e24a5e1-8a658d0d-261127-2`):

- **Scan delay.** Settings > Talk & listen > Scan delay (1, 2, 3, 5, 10 or 15 s): how long the scan stays on a
  channel after the signal ends. The default is 5 s (RadioFoundry's is 2 s). Radios that already saved settings keep
  their stored value until changed.
- **Scan resume and Scan hold.** Scan resume is Carrier (default), Time or Stop. Scan hold (3, 5, 10, 15 or 30 s,
  default 5 s) applies when Scan resume is Time; Scan delay is dimmed in Stop. A change applies to a running scan.
  Dual watch always waits for the signal to end.
- **From RadioFoundry f5f086d2:** APRS keeps the radio's own frequency the first time it sees one.
- **License.** Crucible is now GPL-3.0-or-later. RadioFoundry's files keep their MIT license (`LICENSE-MIT`), and
  `CREDITS.md` lists every project Crucible builds on.
- **Update site.** The [Crucible flasher](https://tinker.wc8mi.net/crucible/) now only updates radios that already run
  RadioFoundry or Crucible; install RadioFoundry first from radiofoundry.org. It has news posts and a Satellite TLE
  page for new satellite data without new firmware, from AMSAT or PE0SAT's amateur list.

Earlier Crucible test builds, November 26-27, 2026:

- Five radios: RT3S / MD-UV380, MD9600, DM1701, DM32 and DB25-D PRO (the DB25-D PRO from November 27; only radios
  of the 909E.DBB batch are tested).
- Digital AGC RX and Digital AGC TX switches in Settings > Talk & listen, on by default with each radio's levels.
- YSF receive: the CTCSS filter no longer rings after loud audio and loses sync.
- NXDN radio default: Rate, RX RAN, TX RAN and Talkgroup for the VFO and channels without their own NXDN settings;
  4800 bit/s by default; Contacts > Talk to in the NXDN VFO; quick settings tile 8 switches the rate.
- DMR and YSF transmit levels set with two radios; the Mic check screen is removed.
- Mic input moved into Crucible's own settings block. Radios on earlier builds keep their setting.

The settings record changed in these builds. If you go back to RadioFoundry, reset the settings.

## Receive display and RSSI correction

- Added the reviewed 13-cell receive S meter with numeric dBm on normal Home in analog and digital modes.
- Added effective transmit-power metadata to dual display, with a compact-screen footer fallback.
- Added Advanced RSSI correction: separate VHF/UHF values, −20 to +20 dB in 1 dB steps, default/reset 0.
- Selected monochrome hollow S cells, filled over cells and inactive underlines, with no S9 divider.
- Kept caller identity, large-text layout, and tested P25/DMR audio behavior.

These changes were in the `kf8s-5172c91a-65621980-261124-11` test package. The shared startup stack gained 64 bytes after
the firmware stack check identified an 8-byte margin shortfall; all four final images pass.

## Current test build

| Item | Value |
|---|---|
| Version | [`v0.1.0`](https://github.com/wc8mi/crucible/releases/tag/v0.1.0) (pre-release) |
| Firmware source | `e877e643` |
| Upstream main included | `5168a55d` |
| ZIP | `crucible-v0.1.0.zip` |
| ZIP SHA-256 | `4bcfe7cebc2de7497a8b1b42725af822725154383d76ce18a70ace4a440c8894` |
| Images | RT3S / MD-UV380 (5 W and 10 W), MD9600, DM1701, DM32, DB25-D PRO |
| AMBE codec | Not included |

The tag identifies the firmware source. Later documentation commits do not change that firmware.
The ZIP and checksum are on the [GitHub release](https://github.com/wc8mi/crucible/releases/tag/v0.1.0).
Update a radio with the [Crucible flasher](https://tinker.wc8mi.net/crucible/).

## November 24, 2026

### Upstream synchronization

- Merged upstream through `5172c91a`, including DMR privacy/TMS key labels, ETSI IP addressing and HIL updates.
- Merged upstream through `bac4b209`, including the FM Home receive-frequency/offset display change (#1043).
- Reviewed upstream's microphone/P25 limiter follow-up (#1054). Kept the fork's existing shared DSP limiter,
  computed DC removal, manual P25 gain handling and -20 dBFS microphone AGC target in overlapping code.
  The AMBE board correction is not added to P25. Existing audio regression tests remain authoritative.
- Included in the current firmware tag listed above.

### Receive display rollback

- Removed the digital RF signal-display enhancement because the added information crowded the display.
- Restored the earlier caller/dual layouts and removed the added RX details preference and measurement metadata.
- Preserved the earlier caller lookup/display improvements, RX/TX AGC, power controls and microphone keys.
- Released in the earlier `kf8s-bac4b209-533acd6c-261124-7` package. The new meter above uses a different
  design. The original feature branch is retained.


### Microphone input and P25 calibration

- Added streaming microphone DC-offset removal before P25 gain and level measurement. The estimate is computed
  from the incoming samples, not a fixed correction measured on one radio. Live DMR/AMBE, FM and receive audio
  retain their existing paths.
- Expanded the temporary codec diagnostic with microphone DC, AC/speech energy, rail counts and comparable
  encoder/decoder measurements. An intermediate diagnostic also supported AMBE; that diagnostic was later removed.
- Replaced the diagnostic with a P25-only calibration that records two seconds of conditioned speech once, tests
  five input targets, and verifies a proposed target against approximately -25 dBFS decoded active speech.
  The recommendation does not save settings. The earlier ten-second parrot is not the current calibration workflow.
- Set the shared live P25 microphone AGC input target to **-20 dBFS** after speech calibration on DM32 and DM1701
  consistently recommended it. Decoded level remains dependent on the speech and decoder; -25 dBFS is not guaranteed.
- Retained calibration behind `CONFIG_P25_PARROT`, default **off**. It needs a compact memory profile when enabled.

Key commits: [`6121f678`](https://github.com/dbehnke/RadioFoundry/commit/6121f678),
[`d199960a`](https://github.com/dbehnke/RadioFoundry/commit/d199960a),
[`db5fa879`](https://github.com/dbehnke/RadioFoundry/commit/db5fa879),
[`97a826cf`](https://github.com/dbehnke/RadioFoundry/commit/97a826cf).

### Transmit power controls

- Added a persistent **Default TX power** setting, with Factory default and all board-supported levels.
- Changed Quick Settings Power from a three-level cycle to a picker containing all supported levels.
- Made inherited channel power explicit with **Radio's (level)** and corrected the temporary-power indicator.
- Added connected-radio power choices to the web codeplug editor, including band-specific names where provided.
  Existing codeplug encoding and calibrated level sets remain unchanged.
- Added a bounded power-metadata diagnostic view and updated the feature-cost documentation.

Key commits: [`897aea14`](https://github.com/dbehnke/RadioFoundry/commit/897aea14),
[`dc8b2fe2`](https://github.com/dbehnke/RadioFoundry/commit/dc8b2fe2).

### Digital RF signal display (later reverted)

- Standardized the existing **-120 to -60 dBm bar** and actual numeric dBm reading across digital caller,
  compact, large-text, dual and receive-audio layouts where space permits. Numbers can extend beyond the bar range.
- Kept caller identity stable instead of rotating it with signal information.
- Added a saved **RX details** choice between Caller details and Signal details for secondary information.
- Added measurement validity, age and source tracking. Held readings carry `~`; unavailable or expired readings
  show dashes. Dual-watch history is not presented as a simultaneous live measurement.
- Preserved DMR per-burst RSSI preference and its existing fallback hold. Limited ordinary meter presentation
  updates to four per second while keeping essential state changes immediate.
- Captured UI view data and change counters together to avoid delaying essential events.
- Isolated UI tick scratch storage from synchronous key paths so the firmware stack check passes without
  increasing the UI task's allocation or reducing its required margins.

Key commits: [`a78929cb`](https://github.com/dbehnke/RadioFoundry/commit/a78929cb),
[`6bb8278c`](https://github.com/dbehnke/RadioFoundry/commit/6bb8278c).

### MD9600 microphone keys and navigation

- Exposed **Mic A-D**, with separate press and hold assignments, in the existing **Settings > Keys** menu.
  Existing panel-key assignments and microphone defaults are preserved.
- Added assignable **Back**, using normal navigation and unsaved-edit confirmation.
- Changed microphone **A/B short press to OK**. A menu hold follows normal OK-hold behavior; a Home hold selects
  the other dual pane or standalone VFO memory. It does not implicitly start dual watch.
- Preserved digit entry, frequency entry, key lock, PTT and microphone A-D DTMF symbols during FM transmission.
- Protected pending editor text when navigating Home, including at maximum menu depth. Expanded real-board
  assignment-row tests and fixed simulator label caching when changing simulated radios.

Key commits: [`4336b41d`](https://github.com/dbehnke/RadioFoundry/commit/4336b41d),
[`e446e6a4`](https://github.com/dbehnke/RadioFoundry/commit/e446e6a4).

## November 23, 2026

### Digital receive audio AGC

- Added one shared receive-speech controller for DMR, P25 and NXDN, with independent per-mode targets, default Off.
- Added optional averaged input/output audio levels in dBFS, before speaker volume.
- Added fast initial acquisition, slower speech tracking, bounded gain, peak/startup protection and a ten-caller
  RAM history. Caller history clears at power-off. FM and local UI sounds are outside this normalization.
- Integrated enabled AGC levels with caller displays and removed redundant AGC stand-in text.

Key commits: [`29ce516e`](https://github.com/dbehnke/RadioFoundry/commit/29ce516e),
[`57ceec26`](https://github.com/dbehnke/RadioFoundry/commit/57ceec26),
[`4b7bf742`](https://github.com/dbehnke/RadioFoundry/commit/4b7bf742).

### Caller identification

- Added optional P25 caller labels from private contacts and the installed DMR ID database, default Off.
  Labels are local lookups; P25 and DMR numeric IDs do not necessarily identify the same person.
- Improved caller identity on P25 and dual Home views, and guarded lookup tests for feature-off configurations.

Key commits: [`9bb6caac`](https://github.com/dbehnke/RadioFoundry/commit/9bb6caac),
[`fa578c7b`](https://github.com/dbehnke/RadioFoundry/commit/fa578c7b),
[`162b7a1d`](https://github.com/dbehnke/RadioFoundry/commit/162b7a1d).

### Transmit audio, diagnostics and upgrade handling

- Added shared transmit leveling and peak protection, P25 microphone PCM AGC, and an optional local P25
  encode/decode/playback diagnostic. The initial P25 target was -25 dBFS; November 24 changed it to -20 dBFS.
- Added microphone DC and speech-level measurements to help distinguish bias from useful microphone audio.
- Protected AES counter state when loading older AGC/settings layouts. Ambiguous legacy counters block AES
  transmission until recovery; factory reset preserves counter/recovery state. Added translated recovery screens.

Key commits: [`9af5cf47`](https://github.com/dbehnke/RadioFoundry/commit/9af5cf47),
[`d541ac0e`](https://github.com/dbehnke/RadioFoundry/commit/d541ac0e),
[`9e83b326`](https://github.com/dbehnke/RadioFoundry/commit/9e83b326),
[`26835ae5`](https://github.com/dbehnke/RadioFoundry/commit/26835ae5).

### Build and package support

- Added private `kf8sfork` build labels with upstream hash, fork hash, date and unique daily sequence.
- Added clean-tree/provenance checks, board/profile validation, warning and stack checks, flasher manifests,
  checksums, and ZIP packaging without embedding stock AMBE codecs.
- Added diagnostic build-profile validation, radio-independent packaging guidance, feature-matrix entries and
  compiled-component/runtime license notices in the packages.

Key commits: [`fef6efbd`](https://github.com/dbehnke/RadioFoundry/commit/fef6efbd),
[`299144ae`](https://github.com/dbehnke/RadioFoundry/commit/299144ae),
[`fc71a4d5`](https://github.com/dbehnke/RadioFoundry/commit/fc71a4d5),
[`f4c1f4e5`](https://github.com/dbehnke/RadioFoundry/commit/f4c1f4e5),
[`1b1ddc06`](https://github.com/dbehnke/RadioFoundry/commit/1b1ddc06).

## Validation and remaining checks

Independent implementation/integration reviews accepted the power, RF display and microphone-key changes.
Host/UI, feature-off, translation, documentation and layout checks passed. The current four-image package passed
firmware size, stack, provenance and checksum checks with warning-free final build logs. C-SKY build-tool crashes
required retries, including a resumed unpublished batch; they did not require source changes.

These checks do not establish hardware RF-power accuracy, RSSI calibration, microphone-key behavior, or on-air
speech quality. Owner testing is the purpose of this build. Earlier on-device calibration informed the P25 target;
it is not a validation of every feature on every radio.
