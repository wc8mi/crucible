<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 David Behnke -->

# Crucible maintainer notes

Crucible is a downstream build of [RadioFoundry](https://github.com/radiofoundry/radiofoundry) for five radios.
[README.md](README.md) is the public face; this page is for whoever builds, releases and merges it, people and coding
agents alike. Everything in [AGENTS.md](AGENTS.md) still applies. This page adds what is different downstream.

## Rules

1. **Upstream first.** A change that would help RadioFoundry goes to RadioFoundry as a pull request from a feature
   branch, and comes back with the next merge. Crucible carries only what upstream won't take or what is ours alone:
   the identity, the radio subset, our defaults, packaging, the flasher site. Keep that carried diff small.
2. **Merge, never rebase.** `main` merges `upstream/main`. Shared history is what keeps those merges cheap. Never
   rewrite `main`.
3. **Thin identity layer.** What users see says Crucible. Internal names stay RadioFoundry's (`rfoundry_*` files,
   `CONFIG_*` switches, `RFOUNDRY_*` macros, the debug link and host link protocols, USB vendor and product ids), so
   upstream changes keep applying. Identity comes from [crucible.toml](crucible.toml) and a few build settings, not
   from edits scattered through shared code.
4. **Five radios.** `crucible.toml [radios]` lists them. Releases, CI, the generated docs and the flasher read that
   list. Other boards' files stay untouched: deleting them would turn every upstream change to them into a conflict.
   This list is the one exception to AGENTS.md's "no list of radios" rule.
5. **Keep the notices.** `LICENSE`, `AUTHORS`, `CONTRIBUTORS` and every SPDX and copyright line stay. A file we change
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

Every image, manifest and ZIP in one batch uses:

```
crucible-<upstream-main-short-hash>-<main-short-hash>-<yymmdd>-<sequence>
```

The ZIP is `crucible-<label>.zip`; its README says the build is Crucible's and sends help to Crucible's issues. The
old `kf8sfork` and `kf8sforkysf` branches still build every board under the `kf8s-` and `kf8sysf-` labels.

Hashes have eight characters. Dates use America/Detroit. The sequence starts at 1 each day and increments once
per build attempt, including failed attempts, so a label is never reused. The counter is local Git metadata shared
by worktrees. Keep it when moving the build setup. The actual Git commit, build-source metadata and crash ID keep
their normal meaning. The full label is the release field; the old CPS Git field stays a short commit.

Output is under `build/kf8s/<label>/`. The ZIP includes every image and manifest, `SHA256SUMS`, build metadata,
`LICENSE`, `AUTHORS`, `CONTRIBUTORS` and third-party licenses. Keep those notices when sharing it, as
[FORKING.md](FORKING.md) requires. No AMBE codec is included.

Host tests and firmware checks do not replace listening tests on the radios. No build command flashes or keys a radio.

## Publishing a release

Finish building and verifying the package first, then publish it in
[wc8mi/crucible releases](https://github.com/wc8mi/crucible/releases). Use the package label as the annotated Git tag,
pointing to the exact firmware commit in `build.json`. Push the branch and tag without changing an existing release
tag. Attach the ZIP and its `.zip.sha256` sidecar, keep all license notices, and include the radios, changes,
validation limits and a link to the [usage guide](docs/kf8sfork/README.md). Mark every release a pre-release until a build is stable (YSF is still incomplete). The flasher lists
pre-releases when a radio has no full release. The
[Crucible flasher](https://tinker.wc8mi.net/crucible/) lists a release once its site is rebuilt.

## Flasher page

The [Crucible flasher](https://tinker.wc8mi.net/crucible/) is one static folder: RadioFoundry's flasher page with
Crucible's shell and look, offering only the releases above. Build it after publishing a release:

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

The look lives in `site/crucible/` (`crucible.css`, the self-hosted Barlow Condensed heading face, OFL-1.1). It loads
after the shared stylesheets and changes no markup, so upstream's flasher keeps merging. The design record is
`DESIGN.md`; the product notes behind it are `PRODUCT.md`.

Documentation for testers: the [usage and testing guide](docs/kf8sfork/README.md) and the
[changelog](docs/kf8sfork/CHANGELOG.md).
