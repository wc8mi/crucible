<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 David Behnke -->

# Crucible maintainer notes

Crucible is a downstream build of [RadioFoundry](https://github.com/radiofoundry/radiofoundry) for five radios.
[README.md](README.md) is the public face; this page is for whoever builds, releases and merges it, people and coding
agents alike. Everything in [AGENTS.md](AGENTS.md) still applies. This page adds what is different downstream.

## Rules

1. **Our own project, upstream's changes welcome.** Crucible is GPL-3.0-or-later and sends nothing back to
   RadioFoundry. We still merge `upstream/main` (MIT code may go into a GPL work) and take the best of other firmware
   under GPL-3.0-compatible licenses, with credit ([CREDITS.md](CREDITS.md), [AGENTS.md](AGENTS.md) rule 1). Keep
   the diff in RadioFoundry's own files small, so the merges stay cheap.
2. **Merge, never rebase.** `main` merges `upstream/main`. Shared history is what keeps those merges cheap. Never
   rewrite `main`.
3. **Thin identity layer.** What users see says Crucible. Internal names stay RadioFoundry's (`rfoundry_*` files,
   `CONFIG_*` switches, `RFOUNDRY_*` macros, the debug link and host link protocols, USB vendor and product ids), so
   upstream changes keep applying. Identity comes from [crucible.toml](crucible.toml) and a few build settings, not
   from edits scattered through shared code.
4. **Five radios.** `crucible.toml [radios]` lists them. Releases, CI, the generated docs and the flasher read that
   list. Other boards' files stay untouched: deleting them would turn every upstream change to them into a conflict.
   This list is the one exception to AGENTS.md's "no list of radios" rule.
5. **Keep the notices, give credit.** `LICENSE` (GPL-3.0), `LICENSE-MIT` (RadioFoundry's), `AUTHORS`,
   `CONTRIBUTORS`, [CREDITS.md](CREDITS.md) and every SPDX and copyright line stay. A file we change
   gets `Copyright (c) 2026 David Behnke` below the existing lines, never in place of them ([FORKING.md](FORKING.md)).
6. **Not RadioFoundry.** Nothing we publish may look like it comes from, or is supported by, the RadioFoundry project.
   Support questions come to us. Crucible sends nothing to RadioFoundry's services (build API, flash log, error
   reports).

## Branches and remotes

| Name | What |
|---|---|
| `main` | What we release. Every release is tagged on it. |
| feature branches | Work in progress; upstream candidates are branched from `upstream/main`, not from `main`. |
| `origin` | `https://github.com/wc8mi/crucible` |
| `upstream` | `https://github.com/radiofoundry/radiofoundry` |

## Merging RadioFoundry

The Upstream sync workflow does this every Monday and opens a pull request, or an issue when it can't. By hand:

```
tools/crucible_sync.sh        # fetches upstream, merges into sync/upstream-<date>-<hash>; pushes nothing
JOBS=8 tools/ci.sh            # ends with "all checks passed"
```

The script keeps files Crucible deleted deleted (RadioFoundry's workflows, the feature and radio request forms) when
upstream changes them. Any other conflict is for a person: resolve it in favor of upstream's behavior unless the
carried change is the point. Our files that upstream also has (README.md, the issue and PR templates) keep our side.
Merge the sync branch into `main` through a pull request.

After the merge, check:

- `python3 tools/boards.py docs` changes nothing, or commit what it writes.
- A new feature switch upstream added: on or off in [config/crucible.mk](config/crucible.mk), and in crucible.toml
  `[ci] matrix` if a release might turn it off.
- A UI change: regenerate the gallery (`make -C tests/host ui-gallery`), and look at the boot and About screens.

## Features and defaults

Nothing is removed from the tree. A feature Crucible doesn't ship is off by default in
[config/crucible.mk](config/crucible.mk), read after each board's own `config.mk`; the board files stay upstream's.

| Feature | In Crucible |
|---|---|
| FM, DMR, P25, NXDN | On |
| YSF | On; incomplete, so every release is a pre-release until a build is stable |
| Trunking (DMR Tier III, P25 trunked) | Off (`config/crucible.mk`), and out of the maximal build |
| MMDVM hotspot with split duplex, single frequency repeater, zone sharing | Off (`config/crucible.mk`), and out of the maximal build: their flash goes to YSF VW and M17 |
| Scan delay | 5 s by default (RadioFoundry's is 2 s): the value of `RADIO_SCAN_DELAY_MS` in `core/radio/radio.h`; keep 5000u on a merge conflict |
| IP services, satellite tracking | On, best effort: upstream maintains them; their host tests run, but a failure alone doesn't block a release |

## CI

[Crucible CI](.github/workflows/crucible-ci.yml) runs on every pull request to `main` and every push to it.
RadioFoundry's workflows are deleted here: their nightly and hardware jobs need runners Crucible doesn't have.

| Part | What |
|---|---|
| Boards | Only crucible.toml `[radios]`: `tools/ci.sh` narrows any selection to them |
| Builds | Each board's default and maximal configuration, plus each switch in crucible.toml `[ci] matrix` built off |
| Host tests | All of them, including the audio tests (RX AGC, mic gain, beeps, the synthesized voice set) |
| Checks | Python, docs, translations, licenses, the web flasher tests, the generated mark (`tools/crucible_mark.py --check`) |

Locally, `JOBS=8 tools/ci.sh` runs the same with Crucible's boards. Listening tests on the five radios go in the
bring-up records (`tools/bringup.py`); recordings are never committed.

## Build and package

Commit reviewed changes on `main` first. Initialize the submodules and install both toolchains as described in
[building](docs/dev/building.md). Then run:

```
python3 tools/kf8s_build.py --self-test
python3 tools/kf8s_build.py --jobs 8
```

The wrapper refuses other branches and dirty trees. It fetches RadioFoundry's main (the remote whose URL is
crucible.toml `upstream`, else `origin`) and requires that commit to be an ancestor of the build. It builds each
radio in crucible.toml `[radios]` with every band/power option (`--board` narrows that), runs each image's stack
check, rejects build warnings, and packages codec-free images with flasher manifests and license notices.

Every image, manifest and ZIP in one batch is named by its version (see [Versions](#versions)), for example
`v0.1.0`. The ZIP is `crucible-<version>.zip`; its README says the build
is Crucible's and sends help to Crucible's issues. The version is the release field the radio shows (splash, About,
the debug link's `release=`); the Git commit, build-source metadata and crash ID keep their normal meaning, and
`build.json` records the upstream and Crucible commits. The old `kf8sfork` and `kf8sforkysf` branches still build every
board under their dated `kf8s-` and `kf8sysf-` labels.

Output is under `build/kf8s/<version>/`. The ZIP includes every image and manifest, `SHA256SUMS`, build metadata,
`LICENSE`, `LICENSE-MIT`, `CREDITS.md`, `AUTHORS`, `CONTRIBUTORS` and third-party licenses. Keep those notices when sharing it, as
[FORKING.md](FORKING.md) requires. No AMBE codec is included.

Host tests and firmware checks do not replace listening tests on the radios. No build command flashes or keys a radio.

## Versions

Crucible's versions follow [semantic versioning](https://semver.org), starting at 0.1.0. A version is plain
`vMAJOR.MINOR.PATCH`: no `-beta` or other suffix. crucible.toml `[release]` holds the next one; it changes only by a
commit.

| Key | What |
|---|---|
| `version` | The next release. `tools/kf8s_build.py` names the build `v<version>` and refuses a version already tagged here or on origin. |
| `prerelease` | `true` marks every release a GitHub pre-release, a test release: the flasher lists it under "Show test builds", and anyway while a radio has no full release. Set it `false` once a build is stable (YSF is still incomplete). |

- After each release, raise the patch in its own commit (0.1.0 to 0.1.1): the resting value assumes fixes only.
- The pull request that merges the first new feature since the last release raises the minor in the same commit
  (0.1.1 to 0.2.0), so `version` always matches what is queued. An upstream merge that adds features counts. 1.0.0 and
  later majors are for changes that break settings or codeplugs; before 1.0.0 a minor may also change them, so say so
  in the notes.
- A failed build reuses its version: remove `build/kf8s/<version>/` and build again. Once a version is tagged it is
  never rebuilt.
- Tags are `v` and the version, nothing else: no build metadata (`+...`), because the radio shows the name.
  Upstream's calendar tags (`v26.11`) never look like ours, and `tools/crucible.py tag` checks one.
- Older names stay as tags only, so a report from one of those builds still finds its source: the dated test releases
  (`crucible-<hash>-<hash>-<yymmdd>-<n>`) and `v0.1.0-beta.1`, rebuilt as `v0.1.0`.

## Publishing a release

Finish building and verifying the package first, with `main` pushed. Then:

```
python3 tools/crucible_release.py notes build/kf8s/<version> > notes.md   # a draft: radios, commits, changes, limits
python3 tools/crucible_release.py publish build/kf8s/<version> --notes notes.md --dry-run
python3 tools/crucible_release.py publish build/kf8s/<version> --notes notes.md
```

Read and edit the draft first: the flasher shows the notes next to the version. `publish` refuses a package whose
ZIP doesn't match its sidecar, whose commit is not on `origin/main`, or whose release already exists. It makes the
annotated tag on the commit in `build.json`, pushes the tag, and creates the release in
[wc8mi/crucible releases](https://github.com/wc8mi/crucible/releases) with the ZIP and its `.zip.sha256` attached: a
pre-release while `[release] prerelease` was true for the build, else the latest release. Keep all license notices
and the link to the [usage guide](docs/kf8sfork/README.md) in the notes. Then raise `[release] version`. The
[Crucible flasher](https://tinker.wc8mi.net/crucible/) lists a release once its site is rebuilt.

## Flasher page

The [Crucible flasher](https://tinker.wc8mi.net/crucible/) is one static folder: RadioFoundry's flasher page with
Crucible's shell and look, offering only the releases above. It only updates: Crucible supports an upgrade from a
RadioFoundry base, never a first install on the manufacturer's firmware (KF8S, 2026-11-27; [ADR 001](docs/crucible/adr/001-installer-two-paths.md),
revision 1). The front page sends a first install to [RadioFoundry's flash page](https://radiofoundry.org/flash/index.html)
as a plain link; the site never calls a RadioFoundry service. Build it after publishing a release:

```
uv run --no-project --with-requirements site/requirements.txt python3 tools/crucible_site.py
```

The result is `build/crucible-site/`. Copy it to `/crucible/` on the tinker host. The tool:

- builds the shared site and keeps only the flash page and the files it loads
- downloads each `crucible-*.zip` release (with `gh`, so a private repository works), checks it against its `.zip.sha256`
  and `SHA256SUMS`, and writes the images for the five radios with `releases/index.json` (`--package ZIP` adds a local
  one, `--no-github` skips the download)
- sets `data-reporting="off"`: the page sends nothing to RadioFoundry's services, and a problem report is saved as a
  file for a wc8mi/crucible issue
- says Crucible wherever the flasher's English says RadioFoundry, and credits RadioFoundry in the footer
- builds the shared Satellite TLE page at `tools/satellite/index.html` in the same shell. The front page links it, and
  the flasher offers it after an update, so a radio with the satellite feature gets new satellite data without new
  firmware. Its Load latest TLEs button reads the AMSAT daily bulletin from `../../api/amsat-tle` (`data-amsat-api`)
  and PE0SAT's amateur list from `../../api/pe0sat-tle` (`data-pe0sat-api`), and keeps the newest TLE per satellite.
  On the tinker host these are `/crucible/api/amsat-tle` and `/crucible/api/pe0sat-tle`: GET-only proxies in Caddy
  that send no visitor headers. PE0SAT (`http://tle.pe0sat.nl/kepler/amateur.txt`) serves HTTP only; the proxy gives the
  page HTTPS. The page
  allows requests to this site only. The satellite frequencies are the page's own table, not a download
- builds the shared Codeplug Editor at `channels/index.html` in the same shell, titled Channels. The front page links
  it. The editor's root carries three switches: `data-targets="all"` checks each channel on every Crucible radio
  (the editor's **Made for** picker, read from `radios.json` `codeplug.profiles`); `data-update="off"` drops the offer
  of RadioFoundry releases after a read; `data-rid="file"` builds the DMR ID database from a `user.csv` file only,
  since the RadioID API needs a proxy the page's policy does not allow. CSV import and export work as upstream's
- fails the build if a page links to a local file that is not there

The look lives in `site/crucible/` (`crucible.css`, the self-hosted Barlow Condensed heading face, OFL-1.1). It loads
after the shared stylesheets and changes no markup, so upstream's flasher keeps merging. The design record is
`DESIGN.md`; the product notes behind it are `PRODUCT.md`.

### News

The front page shows a list of news posts, newest first. Add one post for each release or other change that users
see. A post is one Markdown file in `site/crucible/news/`, named `YYYY-MM-DD-slug.md` (the slug is lowercase letters,
digits and hyphens):

```
<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 David Behnke -->
---
date: 2026-11-27
title: A fifth radio and the NXDN radio default
release: crucible-4e24a5e1-8a658d0d-261127-2
seq: 1
---
The post body, in Markdown.
```

| Key | Need | Use |
|-----|------|-----|
| `date` | yes | The same date as the file name. |
| `title` | yes | 1 to 10 words. |
| `release` | no | The release tag. The post links to that GitHub release. |
| `seq` | no | The order of posts on the same date. The highest number shows first. |

The body is CommonMark. Raw HTML is shown as text. The ADR 001 word rules apply to the title (no banned word), not to
the body.

To name a radio, write `{{boards/<id>}}` in the title or the body, with the radio's folder name under `boards/`. The
build puts in the radio's maker and model as the front page's radio list shows them. An id that is not a Crucible radio fails the build. The title's 10 words are counted
after the name is put in. Do not type a radio's name in a post: `tools/check_generic.py` refuses it.

The build (`tools/crucible_site.py`) puts the newest five posts in `index.html`, so they show without JavaScript. It
writes all the posts to `data/news.json` (`crucible-news/1`: `first`, then `posts` with `id`, `date`, `title`,
`release` and `html`) and an Atom feed to `news.xml`. When there are more than five posts, a More news button loads
five more on each press and goes away after the last one. Each post has the anchor `#news-<date>-<slug>`; a link to
an older post loads it. `--self-test` checks the posts. Rebuild and deploy the site after you add a post.

Documentation for testers: the [usage and testing guide](docs/kf8sfork/README.md) and the
[changelog](docs/kf8sfork/CHANGELOG.md).
