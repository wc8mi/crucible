<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 David Behnke -->

# 001. The installer page: two paths, a work card, plain words

| | |
|---|---|
| Status | Accepted (KF8S, 2026-11-26) |
| Date | 2026-11-26 |
| Decides | The page at tinker.wc8mi.net/crucible/ |
| Material | [001/prototype.html](001/prototype.html) (open with a local web server from the repository root), [001/screens/](001/screens/) |

## Context

- Most Crucible users operate a radio like an appliance. They do not know "DFU", "codec" or "stock firmware".
- Today's page is RadioFoundry's flasher with Crucible's colors. It shows six tasks, a stepper and many options. It
  was made for a project with many radios and a build service.
- In release mode (Crucible's page) the shared flasher can only **update**. A first install gets its image from
  RadioFoundry's build service (`requestBuild`, `site/static/js/flasher/app.js`). Crucible must never call that
  service.
- Each radio installs differently:

| Radio | Update mode | Backup before install | Firmware file needed |
|---|---|---|---|
| DM-1701 | Hold PTT and SK1, turn on. Screen stays dark. | None. Save channels with the CPS. | Baofeng V02.06 or Radioddity V02.01 to V02.03 |
| MD-UV380 | Hold PTT and SK1, turn on. Screen stays dark. Pick 5 W or 10 W from the label. | None. Save channels with the CPS. | Any supported TYT or Retevis file |
| DM-32UV | Cable in the speaker-mic jack. Turn on with the volume knob, no keys. LED blinks yellow slowly; at the end it flashes green three times. | The installer reads the radio memory (required). | Not needed: the codec comes from the backup |
| MD-9600 | Disconnect DC. Hold P1 and the orange key, connect DC. Backlight blinks. | Optional "Back up now" before install. | A file from the board family (V1-V3, V4, V4A/V5) |

## Decision

1. **A gate before anything.** A browser without Web Serial and WebUSB sees one screen: "Use Chrome or Edge" on a
   Windows, Mac or Linux computer. Phones are not offered at launch. Nothing else loads.
2. **Terms once per computer.** Four numbered items (experimental, no warranty, an install can cause damage, the user
   is responsible) and one checkbox. The checkbox starts clear; Continue stays disabled until it is set. The answer is kept in `localStorage`. The footer repeats "No warranty. For
   tinkerers." on every screen.
3. **Two doors.** The home screen asks one question and offers two choices: **Install Crucible** (the radio has the
   manufacturer's firmware) and **Update Crucible** (the radio has Crucible or RadioFoundry). The two doors have equal
   weight. Backup, restore and help are small links below.
4. **Install is a work card.** The whole procedure is one numbered list. Done steps close to one line with a check and
   their result (DM-1701, Saved, V02.06). The current step is open. Later steps show only their names. The user always
   sees where they are and what comes next. Steps: Radio, Things you need, Save your channels, Firmware file, Update
   mode, Install. The list adapts to the radio:

   | Radio | Steps |
   |---|---|
   | DM-1701, MD-UV380 | Radio, Things you need, Save your channels, Firmware file, Update mode, Install |
   | DM-32UV | Radio, Things you need, Update mode, Backup, Install (the backup reads the radio in update mode) |
   | MD-9600 | Radio, Things you need, Backup, Firmware file, Update mode, Install |

5. **Backup is a gate on Install.** On the DM-1701 and MD-UV380 the user confirms "I saved my channels in a file"
   before Next works. A user without the CPS opens "I do not have the CPS", reads a caution and continues on purpose.
   On the DM-32UV the installer makes the backup itself and the browser saves it. On the MD-9600 the installer makes
   the backup with the manufacturer's firmware still running. After the install, the done screen offers "Make a backup
   of the radio" first.
6. **Update is scan first.** Connect; the page reads the radio and shows one recommended release with a short list
   of changes and one Update button. A RadioFoundry radio gets the same screen with one line: "This update replaces
   RadioFoundry with Crucible." A backup is an optional link, never a step.
7. **Advanced users stay fast.** "Other versions" is closed by default. Open, it lists every release for the radio
   and links to file details and checksums. The button stays disabled until the user picks a version other than the one
   on the radio. A caution shows only when the pick is older. Connect, open, pick, update: four actions.
8. **Failures have their own screens.** The radio is not found; the file is not correct; the install stopped; the radio
   has the manufacturer's firmware (on Update). Each starts with STOP, says what happened and gives numbered steps. The install-stopped screen shows the update-mode sign and step number of the selected radio.
9. **Words follow ASD-STE100** (Issue 9). Rules in [Words](#words).
10. **The look stays the foundry.** The hot-core band, iron or greensand ground, Barlow Condensed headings, and the
   pour as every progress bar ([DESIGN.md](../../../DESIGN.md)). The step list's progress bar is a pour too.

## Options considered

Seven structures were ranked; a random deal (Impeccable `concept-seed`, surface scope, Operate; seed reference
`seed:67fd2aeb5bd68219`) put three on the table. The work card led the deal. The maintainer delegated the choice.

| Option | What it is | Result |
|---|---|---|
| **Work card** (chosen) | The full procedure as a numbered list, the current step open | Shows the whole job up front. An appliance operator sees the end before the start. Works the same on a phone. |
| Station strip | A horizontal row of stations (Check, Backup, Codec, Pour, Done) above one large stage | Strong on a wide screen, but the strip breaks on a phone and hides what each station asks for. |
| Conversation | One question at a time; answers stack as a transcript | Friendly, but the user cannot see how long it is. A long install feels endless. |
| Keep upstream's flasher, restyled | Today's page | Too many choices and words for the audience. Cannot install in release mode. |

## Consequences

- **Crucible owns a front end.** `site/crucible/installer/` holds the new page and its screens. It calls the shared
  flasher's engine modules (`link.js`, `dfu.js`, the C7 backend, `releases.js`, `origbackup.js`, `progress`) and does
  not fork them. Upstream fixes to the engine keep merging. Upstream's own page stays in the tree, unused by Crucible.
- **Install from release images is new engine work.** The install step list (`radio, build, now, codec, write, done`)
  takes its image from `requestBuild()`. Crucible needs an image source that reads the release index instead. This is
  the main engineering item and the highest risk. It is tested on each radio with the owner present.
- **Close the build-service path.** In release mode, Troubleshooting's "Reinstall" may reach `requestBuild()`. Until
  the new page ships, the build step must refuse in release mode. (Unconfirmed in a browser; check first.)
- **Fewer recovery paths on the main screens.** "Radio stuck in update mode", Restore and Troubleshooting live behind
  Help. They keep upstream's screens until Crucible rewrites them.
- **Translation.** English only. All text goes through the existing `tr()` keys so a later translation is possible.

## Words

The page uses one term for one thing. Do not use the words in the right column.

| Use | Do not use |
|---|---|
| install (first time), update (later) | flash, burn, upgrade |
| the manufacturer's firmware | stock firmware, OEM, factory firmware |
| firmware file | stock file, image, .bin |
| update mode | bootloader, DFU, IAP |
| voice codec (only where the user must act) | AMBE, vocoder |
| programming cable | data cable, USB lead |
| save your channels | back up the codeplug |
| test release | pre-release, beta, nightly |

Rules applied to every string:

- Instructions are imperative, one action each, at most 20 words. Descriptions are at most 25 words.
- No -ing verb forms, no phrasal verbs ("plug in", "set up", "back up" as a verb), no contractions.
- Alerts start with a signal word. **CAUTION**: a risk to the radio or data. **STOP**: the task cannot continue. The
  command comes first, then the result.
- Notes give information, never an instruction.
- Labels on buttons are verbs: Install, Update, Connect, Next, Try again.

## Screens

Desktop, dark theme. The light theme and phone captures are in [001/screens/](001/screens/).

| | |
|---|---|
| ![Gate](001/screens/01-gate-dark-desktop.png) 1. Gate | ![Terms](001/screens/02-terms-dark-desktop.png) 2. Terms |
| ![Home](001/screens/03-home-dark-desktop.png) 3. Home | ![Install: radio](001/screens/04-i-radio-dark-desktop.png) 4. Install: radio |
| ![Install: MD-UV380 power](001/screens/05-i-radio-power-dark-desktop.png) 5. Install: MD-UV380 power | ![Install: things you need](001/screens/06-i-need-dark-desktop.png) 6. Install: things you need |
| ![Install: save your channels](001/screens/07-i-save-dark-desktop.png) 7. Install: save your channels | ![Install: firmware file](001/screens/08-i-file-dark-desktop.png) 8. Install: firmware file |
| ![Install: file not correct](001/screens/09-e-file-dark-desktop.png) 9. Install: file not correct | ![Install: update mode](001/screens/10-i-mode-dark-desktop.png) 10. Install: update mode |
| ![Install: in progress](001/screens/11-i-pour-dark-desktop.png) 11. Install: in progress | ![Install: stopped](001/screens/12-e-stopped-dark-desktop.png) 12. Install: stopped |
| ![Install: done](001/screens/13-i-done-dark-desktop.png) 13. Install: done | ![DM-32UV: update mode](001/screens/14-i-dm32-mode-dark-desktop.png) 14. DM-32UV: update mode |
| ![DM-32UV: backup](001/screens/15-i-dm32-backup-dark-desktop.png) 15. DM-32UV: backup | ![MD-9600: backup](001/screens/16-i-md9600-backup-dark-desktop.png) 16. MD-9600: backup |
| ![MD-9600: update mode](001/screens/17-i-md9600-mode-dark-desktop.png) 17. MD-9600: update mode | ![Update: connect](001/screens/18-u-connect-dark-desktop.png) 18. Update: connect |
| ![Update: found](001/screens/19-u-found-dark-desktop.png) 19. Update: found | ![Update: other versions](001/screens/20-u-versions-dark-desktop.png) 20. Update: other versions |
| ![Update: manufacturer's firmware](001/screens/21-e-wrongfw-dark-desktop.png) 21. Update: manufacturer's firmware | ![Update: in progress](001/screens/22-u-pour-dark-desktop.png) 22. Update: in progress |
| ![Update: done](001/screens/23-u-done-dark-desktop.png) 23. Update: done | ![Radio not found](001/screens/24-e-notfound-dark-desktop.png) 24. Radio not found |

## Maintainer answers

KF8S answered the open questions on 2026-11-26.

| No. | Question | Answer |
|---|---|---|
| 1 | Photos or line drawings of the radios | Line drawings. They work in both themes, need no photo rights, and can mark the keys to hold. |
| 2 | When the terms show | Once per computer. The footer repeats "No warranty. For tinkerers." |
| 3 | MD-9600 backup | A numbered step that the user can skip behind a caution. |
| 4 | The name for a release that is not stable | "Test release". |
| 5 | "Tinkerers" or "tinkers" | "Tinkerers". |
| 6 | DM-32UV off and on | The volume knob. "Turn the volume knob to OFF/ON." |
| 7 | Android | Computers only at launch. The gate names Chrome and Edge on a computer. Add Android after each radio passes a test from a phone. |

## Plan

| Phase | Work | Needs a radio |
|---|---|---|
| 1 | Gate, terms, home, and the Update path on the shared engine (release mode already updates) | Yes, one update per radio |
| 2 | Install from release images in the engine; the work card for the DM-1701 first | Yes |
| 3 | The other radios' install cards (MD-UV380 family and DB25-D PRO added; DM-32UV and MD-9600 open); error screens for each failure the engine reports | Yes |
| 4 | Retire the restyled upstream page | No |

## Amendment 1: the card in the engine

| | |
|---|---|
| Status | Accepted (KF8S, 2026-11-26) |
| Changes | Decision 4 and the phase 2 plan. The rest of this record stays. |

Phase 2 built the work card on the shared flasher engine (site/static/js/flasher/app.js), not as its own page. The
card is a display mode of the engine's install steps. It reads its steps and words from `data/install-card.json`,
which `tools/crucible_site.py` writes. The engine still does the work: release download, codec, update mode, write,
check. One code path installs and updates, so a fix in one serves both.

| No. | Change | Why |
|---|---|---|
| 1 | The card is a layer on the engine, not its own front end | One install path to test and fix. The engine already handles each radio and each failure. |
| 2 | A **Version** step after Radio | The engine picks the release and the MD-UV380 power class in one view. The done line shows the release. |
| 3 | **Update mode** and **Install** are one step, **Install** | The engine connects in update mode and writes in one view. Two steps would split one action. |
| 4 | A **Check** step at the end | The engine reads the radio back after the write and offers the backup there (decision 5, "after the install"). |
| 5 | The current firmware is set from the card | A radio on the Install path has the manufacturer's firmware. The user is not asked. |
| 6 | Only radios with a card can install | A radio without a card would show the engine's old words. Phase 3 adds the others. |

The DM-1701 steps are now: Radio, Version, Things you need, Save your channels, Firmware file, Install, Check.

Phase 3 adds cards on the same engine. A card step can carry its own caution (`notice`), shown above the step. The
DM-1701, the MD-UV380 family and the DB25-D PRO have cards; the DM-32UV and the MD-9600 do not yet, so they cannot
install from this page.

| Radio | Card steps | Notes |
|---|---|---|
| MD-UV380 family (MD-UV380, MD-UV390, RT3S, MD-UV390 Plus 10 W) | As the DM-1701 | Version shows a caution: choose 10 W by the label; the installer cannot find a wrong choice |
| DB25-D PRO | Radio, Version, Things you need, Save your channels, Update mode, Backup, Install, Check | The HR_C7000 install steps. Update mode starts through the manufacturer's firmware with no keys; the P1 keys are the way in when that fails. Version shows a caution: tested only on 909E.DBB. Backup closes as "Checked": the update-mode route takes no backup |

Not changed: the gate, the terms, the doors, decision 5's checkbox and no-CPS path, the words, the look.

## Review log

| Round | Reviewer | Result |
|---|---|---|
| 1 | STE and UX | Revise: missing DM-32UV and MD-9600 screens, failure screens, terms checkbox state, no-CPS path, wrong restore note, copy rewrites |
| 2 | STE and UX | Revise: wrong-firmware promise, MD-9600 skip, radio-specific stop screen, no-CPS checkbox order, ZIP wording, "example" tag |
| 3 | STE and UX | Accept (two wording nits applied; DM-32UV done screen shows the green flashes in phase 3) |
| 1 | Finish (Impeccable) | Fix: eyebrow tags, condensed face on labels, seed reference, version picker states, door weight, token drift, dark-screen drawing |
| 2 | Finish (Impeccable) | Fix: radio facts box as a two-column grid at every width |
| 3 | Finish (Impeccable) | Fix: e-stopped names the step instead of a number; DM-32UV LED sign gets its own drawing |
| 4 | Finish (Impeccable) | Ship. Not covered: motion (step close, pour edge), the picker's disabled default state (decision 7 only), the Android check (answer 7: computers only at launch) |
