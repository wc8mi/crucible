<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 David Behnke -->

# KF8S Fork Usage and Testing

Updated November 24, 2026. This is the private KF8S test fork of RadioFoundry, maintained by dbehnke (KF8S).
Read the [changelog](CHANGELOG.md) for the history and the [RadioFoundry manual](../user/README.md) for inherited
features. Menu labels below are the English labels; available modes and options depend on the radio and build.

## Which build to test

The current four-radio package is **`radiofoundry-kf8s-5172c91a-65621980-261124-11.zip`**.
Its [Git tag](https://github.com/dbehnke/RadioFoundry/tree/kf8s-5172c91a-65621980-261124-11) points to firmware
commit `65621980`, including upstream main `5172c91a`.

| Radio | Image prefix | Notes |
|---|---|---|
| Retevis RT3S / TYT MD-UV380 | `rfoundry-mduv380-` | Standard 5 W image; not the MD-UV390 Plus 10 W variant |
| TYT MD9600 | `rfoundry-md9600-` | One image covers the supported hardware revisions; retain the correct radio hardware setting |
| Baofeng DM1701 | `rfoundry-dm1701-` | Uses this radio's own image and manifest |
| Baofeng DM32 | `rfoundry-dm32-` | Uses this radio's own image and manifest |

This package includes AES, TX/RX AGC, power controls, microphone-key changes, the new receive S meter,
dual power metadata and Advanced RSSI correction. The temporary P25 calibration/parrot is **off**.
Download the ZIP and checksum from the
[GitHub release](https://github.com/dbehnke/RadioFoundry/releases/tag/kf8s-5172c91a-65621980-261124-11). Its SHA-256 is:

```text
934c2cc8a286f3c8ce38b820963e73c89e4587779c6d3bb8e80ca177c6082c07
```

Back up your current settings/codeplug and follow [Installing](../user/02-installing.md). Use the matching image
and manifest. These images do not embed a stock AMBE codec; the manifests preserve the installed codec regions.
A radio without a working codec still needs the normal supported codec installation procedure.

## New display features to test

The current ZIP includes the new signal meter and RSSI correction. Test these on each radio:

1. Receive FM, DMR and P25; check the bottom S meter, numeric dBm and return of ordinary hints after reception.
2. In dual watch, check that A/B identifies the receiving channel even when PTT selects the other pane. If both
   receive, the star identifies that condition while the letter identifies the meter source.
3. Check power labels for explicit channel power, Default TX power and temporary power, including a split channel.
4. Open **Settings > Advanced > RSSI correction**. Adjust each band independently; verify persistence after restart
   and that **Reset to default** clears both. A known −93 dBm input should reach S9 after correction.
5. Check normal caller identity, unknown caller IDs, large text, key lock, banners and menu navigation. Mono meters
   should use hollow S cells, filled over cells and inactive underlines, with no divider after S9.

Host tests cover scale thresholds, display correction, ownership/freshness, settings migration and UI navigation.
All four firmware images passed size, stack, provenance and warning checks. A separate firmware build with the
meter disabled also passed. The shared startup stack gained 64 bytes to retain its required margin. Radio
validation and RSSI calibration remain owner tests.

## Suggested test order

1. Confirm the build identity in **Settings > Advanced > About** and check ordinary channel/menu operation.
2. Test RX AGC and the caller display while receiving familiar calls.
3. Check power selection and inheritance without transmitting first.
4. Test microphone mappings and navigation on the MD9600.
5. Compare transmit audio and power only on a suitable permitted test channel or bench setup.
6. Restart and confirm that saved preferences remain. Report failures with the template below.

Changing one setting at a time makes comparisons useful. Keep listening volume, microphone distance and the test
phrase consistent when comparing audio.

## Digital receive audio AGC

**Settings > Talk & listen** provides **DMR RX AGC**, **P25 RX AGC** and **NXDN RX AGC** where built.
They start **Off**. Choose **-25 dBFS** as a starting point for the mode you are testing. This is separate from
microphone AGC and from RF signal strength. The speaker volume still controls listening volume; FM is unchanged.

Enable **Settings > Display > RX audio levels** to show averaged input/output levels where the layout supports
them. For example, `AGC -37 > -25 dBFS` means quiet decoded input was raised before volume. These are measured
levels, not promises that every syllable reaches the target. Gain and peak limits can prevent reaching it.

Test a quiet and a louder caller with AGC Off, then repeat with it enabled. Check intelligibility, consistent
average volume, the start of each call, pauses, and any pumping or bursts. Repeated calls can start using remembered
caller levels; ten recent callers are remembered in RAM until power-off. Compare a fresh restart as well.
A pause eventually changes the audio-level readings to dashes. See [RX AGC specification](../spec/rx-agc.md).

## P25 microphone AGC and manual gain

Use **Settings > Talk & listen > Mic AGC**:

- **On:** P25 removes incoming microphone DC bias and uses shared PCM AGC with a **-20 dBFS input target** before
  IMBE encoding. DMR uses the AMBE codec's own AGC, retaining its existing board calibration.
- **Off:** **Mic gain** is the manual control. It has steps 0-10, with 5 as the normal baseline and 2 dB per step.
  P25 still removes DC bias before manual gain. Applicable digital peak protection remains active.

While AGC applies, the manual gain row shows AGC and is dimmed. P25's -20 dBFS is an encoder-input target, not a
fixed decoded speech level and not an RF power value. It was selected after speech calibration on DM32 and DM1701.
The previous DM32 AMBE-specific correction is not applied to the P25 path.

Compare the same normally spoken phrase with Mic AGC Off at gain 5, then On. Ask a receiving operator to compare
clarity and average level, or measure decoded audio with a consistent receiver setup. Report microphone type
(internal or speaker mic), distance, mode, gain/AGC, RX AGC on the receiving end and any clipping or distortion.
Test DMR separately; it does not use the P25 PCM target. Software cannot repair an input already clipped in hardware.

## DMR and YSF TX levels

**Settings > Talk & listen** has **DMR TX level**, **YSF TX level** and **Mic input**:

| Setting | Values | What it does |
|---|---|---|
| DMR TX level | Off, -30 to -12 dBFS (2 dB steps) | The level the leveler holds your voice at before the AMBE encoder, for DMR and NXDN. A level replaces the codec's own AGC. Off keeps Mic gain and Mic AGC |
| YSF TX level | the same | The same for YSF |
| Mic input | -7 to +8 | The microphone's analog level into digital audio. Lower it if your audio sounds distorted at every TX level |

Mic input is dimmed ("Not on this radio") on a radio without an analog microphone step: the DM-32UV and the
DB25-D PRO. On the DB25-D PRO, Crucible's Digital AGC and NXDN radio default have not been tested yet.

Set them with two radios. No diagnostic screen is involved:

1. On radio B, turn on **Settings > Display > RX audio levels** and that mode's **RX AGC**.
2. Talk on radio A at your normal voice and distance.
3. On B's main screen, read the first number in `AGC in > out dBFS`: the level B decoded. The second is after B's
   own AGC; ignore it.
4. Step A's TX level until B reads the level you want. Swap the radios and repeat, once on DMR and once on YSF.

Report the TX level each radio needed for the same decoded reading, with the mode and microphone used. If the radios
agree, that value can become the default AGC target.

## Restored receive display

The recent digital RF signal-display enhancement has been removed from the branch because it crowded the screen.
The earlier caller and dual-display layouts are restored. The added **RX details** setting, held-reading markers
and expanded digital numeric RF meter are removed. Existing signal displays from before that enhancement remain.
Digital RX AGC and its optional **RX audio levels** setting remain available.

Check caller names, destinations and dual panes in normal and large text, with RX audio levels on/off.
Confirm that text has the earlier spacing and that caller identity remains readable. RF dBm measures received
signal strength; audio dBFS measures audio level. Report which one a reading represents.

## Transmit power: radio default, channel override and temporary choice

1. Set **Settings > Talk & listen > Default TX power** to **Factory default** or a supported level.
2. In the [channel editor](../user/07-channels-zones-vfo.md), set **Power** to **Radio's (level)** to inherit it,
   or choose an explicit channel level. Save the channel with **Hold OK** or its **Save** row.
3. On Home, **\*** then **1** opens the full supported-level picker. **OK** applies; **Back** cancels.
   This is temporary on a saved channel. **Hold OK** on Home saves the temporary level to that channel.
4. The VFO retains its own power across restart. An explicit channel level is independent of the radio default.

Changing the radio default reloads the current channel and clears temporary overrides. The power key still cycles
supported levels on radios that expose that key. Labels follow the TX band where supported. The
[radio reference](../user/23-radios.md) lists each radio's levels; labels are nominal where wattmeter checks are pending.

Test an inherited channel and an explicit-power channel. Change the default and verify only the inherited power
follows it. Check picker cancel/save, restart persistence and VHF/UHF labels. On MD9600, the highest labels differ
by band. No arbitrary new wattage levels were added.

The connected **web codeplug editor** also offers the radio's actual level choices. Without known radio metadata,
it uses the generic codeplug scale; those generic level numbers are not watts. Compare a channel written from the
web editor with its on-radio Power row. See [Programming](../user/04-programming.md).

## P25 caller names and dual caller display

Enable **Settings > Talk & listen > P25 contact lookup**; its default is **Off**. P25 callers may then use a name
from private contacts or the installed DMR ID database. Unknown IDs stay numeric. This is a local convenience:
P25 and DMR IDs can identify different people, so do not treat a lookup as verified identity.

Test a known ID, an unknown ID and a long name with lookup On/Off. Check the active caller and destination in
single and dual views, including with RX audio levels enabled. Report missing,
misattributed or stale caller information, with the numeric ID and lookup source.

## MD9600 microphone keys

Open **Settings > Keys**. **Mic A**, **Mic B**, **Mic C** and **Mic D**, and their hold rows, follow P3, P4 and Orange.
Existing assignments are retained. Default microphone actions remain A = previous zone, B = next zone,
C = Monitor, D = Talk target.

Useful assignments include:

| Function | Behavior |
|---|---|
| Back | Normal Back navigation; a hold follows normal Home navigation, preserving unsaved-edit confirmation |
| Scan skip | Temporarily skips the channel that stopped a running scan, until scanning stops; not a permanent scan-list edit |
| Talk target | Opens the current DMR talk-target/contact picker; it does not edit the contact database |
| VFO/Channel | Switches between channel and VFO mode |

Scan skip does not perform nuisance deletion in dual watch, and has no channel to skip when scanning is stopped.
The Talk target picker is DMR-only. Press and hold assignments are independent: set both **Mic A = Back** and
**Mic A hold = Back** for Back/Home navigation on that key. Setting only the press action leaves the hold's
selected or default action unchanged. Available press/hold choices depend on the function; raw held Monitor and
emergency behavior retain their existing rules.

Microphone **A/B** has a dedicated behavior:

| Context | Press | Hold |
|---|---|---|
| Menu, dialog or editor | Normal OK/Select | Normal OK hold, including Save where offered |
| Home with dual display active | Normal OK | Select the other A/B pane |
| Home in standalone VFO mode | Normal OK | Select the other VFO memory without starting dual watch |
| Home on a channel, dual display off | Normal OK | Explains that A/B selection is unavailable |
| Frequency entry | Normal OK | Keeps normal entry/OK behavior |

A/B no longer directly means VFO/Channel. Assign that function to A-D if desired. A long press should not also
trigger the short action. Digits keep their existing entry/DTMF behavior; long-digit shortcuts were not added.
A-D still represent their DTMF symbols during FM transmit.

Try Mic A = Back and Mic A hold = Back, Mic B = Scan skip, Mic D = Talk target, and another slot = VFO/Channel. Confirm
assignment persistence after restart. Check A/B through lists, dialogs and an unsaved channel edit; cancel discard
and verify pending text survives. Verify distinct VFO A/B frequencies, selected dual pane, key lock, PTT and the
unchanged panel keys. The new behavior needs real microphone testing; host tests alone do not establish it.

## Legacy settings and AES recovery

Older settings layouts can reset RX AGC preferences to Off and hide RX audio levels. Recheck those settings after
upgrading. AES transmit-counter state is preserved separately; factory reset does not clear it.

If **AES recovery required** appears, follow the [AES recovery instructions](../user/09-dmr.md#aes-recovery).
**Keep AES counter** is only for a verified latest valid counter for that radio. **Use new AES keys** requires
retiring/replacing all previously used transmit keys first. Cancel leaves AES transmission blocked. Do not clear
or reset the counter simply to make a test proceed. Report an unexpected upgrade/recovery prompt to the maintainer.

## Optional P25 calibration: not in the standard package

The current four-image ZIP has `CONFIG_P25_PARROT=n`; no calibration screen is expected. A special build can
include **Settings > Advanced > P25 calibration**, at a substantial RAM cost and with a compact feature profile.
It is P25-only; the intermediate AMBE diagnostic was removed.

With a calibration-enabled build, choose **Record speech** and speak normally for the two-second capture. The radio
uses the same conditioned recording for five targets and a separate verification pass; allow about 80 seconds.
A verified recommendation targets decoded active speech near -25 dBFS and does not save a setting. Report the
status, suggested target, all AGC/Enc/Dec and Gain/Limited rows, microphone DC and rail counts. No recommendation
means the run did not verify a target. Playback is local; transmission, including Emergency, is suspended while
that screen is open. Raw captured audio is erased after the sweep or exit. See the
[calibration instructions](../user/17-settings.md#p25-calibration).

## Report a test result

Copy this template into your report to KF8S. For a display issue, a photo is useful; avoid publishing private
contacts, keys or personal data visible on the screen.

```text
Radio/model and hardware revision:
Firmware version (About, for example v0.1.0-beta.3):
Internal microphone or speaker mic:
Mode, channel/VFO, single/dual display:
Feature and exact menu setting:
Steps to reproduce:
Expected behavior:
Actual behavior:
Readings (include units: RF dBm or audio dBFS):
Receiving radio/software and its RX AGC setting, if testing TX audio:
Reproduces after restart? Frequency of failure:
Photo/log or comparison with previous build, if available:
```

Useful results include successful checks. Record which combinations you tested; do not assume a pass on one radio
or one mode covers the others. Latest packages have size/stack checks, host tests and independent source review;
RF output accuracy, RSSI accuracy and hardware behavior still need owner verification.
