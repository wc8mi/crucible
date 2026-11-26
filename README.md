<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 David Behnke -->

# Crucible

**English** · [简体中文](README.zh.md)

**Experimental firmware for four DMR radios, by KF8S.** Crucible is a downstream build of
[RadioFoundry](https://github.com/radiofoundry/radiofoundry): the same MIT-licensed core, cut down to the radios we
use and tuned the way we like them.

> [!IMPORTANT]
> **Crucible is not RadioFoundry.** The RadioFoundry project doesn't make, review or support it. Please don't ask
> RadioFoundry for help with a radio that runs Crucible. Ask us: [Getting help](#getting-help).

## What Crucible is

- **A subset.** Four radios, listed below. Other radios RadioFoundry supports aren't built or tested here.
- **Opinionated.** Features we want are on, some defaults differ, and changes run on our radios before they go
  anywhere else. What's different is in the [changelog](docs/kf8sfork/CHANGELOG.md).
- **Downstream.** We merge RadioFoundry's main branch regularly. Each release says which RadioFoundry commit it
  includes.
- **Experimental.** Releases are tested on our radios, not on every hardware revision. Keep a backup.

## Radios

<!-- generated: tools/boards.py docs radios; edit boards/*/board.toml [status] and docs/spec/radios.md -->
| Radio | Builds | Host tests | On the radio | Support |
|---|:---:|:---:|:---:|---|
| [Baofeng DM-32UV](docs/testing/dm32-bringup.md) | ● | ● | ● | FM and DMR voice both ways, CTCSS/DCS, APRS both ways, GPS data, the hotspot and each split duplex half work on air. Open: FM deviation at 12.5 kHz and an outdoor GPS fix. |
| [Baofeng DM-1701](docs/spec/hardware-dm1701.md), Retevis RT84 | ● | ● | ● | FM, DMR voice and data, text messages, alerts, emergency, scan, the hotspot, single frequency repeating, each split duplex half and the USB network services work on air. Open: standalone SFR and a two-radio split duplex pair. |
| [TYT MD-UV380](docs/testing/mduv380-bringup.md), MD-UV390, Retevis RT3S, MD-UV390 Plus 10 W | ● | ● | ● | Built on the UV380 family code, with the MD-UV380's keys, display, power tables and GPS. Run on radios by community testers. |
| [TYT MD-9600](docs/spec/hardware-md9600.md), Retevis RT90 (mobile) | ● | ● | ● | One image serves every board revision. On a V1-V3 radio, FM and DMR on both bands, DMR voice and data, text messages, roaming, scan, dual watch, APRS, the hotspot, each split duplex half and the USB network services work on air, with every power level checked on a wattmeter on both bands. V4A/V5 boards are detected; on older boards the radio asks for the revision once. Open: V4 boards have not run on a radio, one V4A/V5 report has DMR transmit dropping out (#750), and SFR loses inbound voice frames. |
<!-- end generated -->

## Install and update

**[Crucible flasher](https://tinker.wc8mi.net/crucible/)**, in Chrome or Edge: connect the radio, pick a release,
flash. It updates a radio that already runs Crucible or RadioFoundry, keeps the codec and settings, and backs up
and restores.

Releases are on the [releases page](https://github.com/wc8mi/crucible/releases). Each zip holds every radio's image
and its manifest, with the license notices.

## Getting help

- **Crucible problems:** [open an issue](https://github.com/wc8mi/crucible/issues) here. Include the radio and the
  version from **Settings > Advanced > About**.
- **How a feature works:** RadioFoundry's [user manual](https://github.com/radiofoundry/radiofoundry/tree/main/docs/user)
  covers the shared features. Where Crucible differs, our [usage guide](docs/kf8sfork/README.md) says so.

## Contributing

**Contribute to [RadioFoundry](https://github.com/radiofoundry/radiofoundry), not to Crucible.** A fix or feature
that helps any radio belongs upstream, where everyone gets it and it carries your name. Crucible picks it up at the
next merge. Start with their [contributing guide](https://github.com/radiofoundry/radiofoundry/blob/main/docs/dev/contributing.md).

Crucible doesn't take outside pull requests. Bug reports about Crucible itself are welcome. If the bug is in
RadioFoundry too, report it there.

## License and credit

MIT ([LICENSE](LICENSE)). Crucible is built on RadioFoundry, copyright The RadioFoundry Authors ([AUTHORS](AUTHORS),
[CONTRIBUTORS](CONTRIBUTORS)). Crucible's changes are copyright David Behnke. The license notices stay with every
file, image and release, as [FORKING.md](FORKING.md) describes. Third-party licenses are in [LICENSES/](LICENSES/).

Crucible's maintainer notes (building, releasing, merging RadioFoundry): [CRUCIBLE.md](CRUCIBLE.md).

## Layout

| Path | What |
|---|---|
| `core/` | Radio-independent code: DMR stack (`core/dmr`), radio state machine (`core/radio`), UI (`core/ui`), hotspot, host link, codeplug, settings, CPS, debug link |
| `hal/` | Interfaces the core uses (modem, RF, audio, codec, flash, display, input), and the `board_t` descriptor |
| `drivers/` | Chip drivers shared across radios |
| `platform/` | Per-SoC startup, clocks, interrupts and RTOS glue (`stm32f4`, `hrc7000`) |
| `boards/` | One directory per radio: board descriptor, pins, feature defaults, linker script, codec hosting |
| `boot/` | First stages in front of the application, for radios whose loader starts it directly (`boot/db25d`) |
| `config/` | Build-time feature switches |
| `third_party/` | Permissively licensed upstream code, as submodules (FreeRTOS, ST HAL and CMSIS, TinyUSB) |
| `tools/` | Flashing (`rfoundry`), release packaging, codec merge, CPS client, debug link client (`rfoundry_debug.py`), CI, SDR bench tools |
| `tests/host/` | Unit and SIL tests, the UI simulator (`tests/host/sim`) |
| `tests/hil/` | HIL tests: pytest against a radio and the SDR bench |
| `docs/spec/` | Behavior, hardware facts and protocols; `gotchas.md` lists past bugs by subsystem |
| `docs/ux/` | UI principles, flows, the screenshot gallery |
| `docs/user/`, `docs/dev/`, `docs/testing/` | User manual (built to PDF on release tags), developer documentation, test procedures |
