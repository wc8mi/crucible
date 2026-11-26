<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 David Behnke -->

# Crucible maintainer notes

Crucible is a downstream build of [RadioFoundry](https://github.com/radiofoundry/radiofoundry) for four radios.
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
4. **Four radios.** `crucible.toml [radios]` lists them. Releases, CI, the generated docs and the flasher read that
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

```
git fetch upstream
git switch -c sync/upstream-$(date +%y%m%d) main
git merge upstream/main
JOBS=8 tools/ci.sh            # ends with "all checks passed"
```

Resolve conflicts in favor of upstream's behavior unless the carried change is the point. Our files that upstream
also has (README.md, the issue templates) keep our side. Merge the sync branch into `main` through a pull request.

## Build and package

Commit reviewed changes on `main` first. Initialize the submodules and install both toolchains as described in
[building](docs/dev/building.md). Then run:

```
python3 tools/kf8s_build.py --self-test
python3 tools/kf8s_build.py --jobs 8
```

The wrapper refuses other branches and dirty trees. It fetches upstream main and requires that commit to be an
ancestor of the build. It builds every board and band/power option in the board registry, runs each image's stack
check, rejects build warnings, and packages codec-free images with flasher manifests and license notices.

Every image, manifest and ZIP in one batch uses:

```
kf8s-<upstream-main-short-hash>-<main-short-hash>-<yymmdd>-<sequence>
```

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
validation limits and a link to the [usage guide](docs/kf8sfork/README.md). Mark test firmware as a pre-release. The
[Crucible flasher](https://tinker.wc8mi.net/crucible/) lists a release once its site is rebuilt.

Documentation for testers: the [usage and testing guide](docs/kf8sfork/README.md) and the
[changelog](docs/kf8sfork/CHANGELOG.md).
