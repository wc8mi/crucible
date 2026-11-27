#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 David Behnke
"""Publish a package tools/kf8s_build.py made as a GitHub release of Crucible (CRUCIBLE.md "Publishing a release").

    tools/crucible_release.py notes build/kf8s/v0.1.0-beta.1 > notes.md      draft release notes to edit
    tools/crucible_release.py publish build/kf8s/v0.1.0-beta.1 --notes notes.md [--dry-run]
    tools/crucible_release.py --self-test

publish checks the package before anything leaves this computer: the tag is a Crucible version (v0.1.0, or
v0.1.0-beta.N), the ZIP matches its .zip.sha256 sidecar, and the firmware commit in build.json is on origin's main.
Then it makes the annotated tag on that commit, pushes the tag, and creates the release with the ZIP and its sidecar
attached. A beta is marked a pre-release: the flasher hides it behind "Show test versions" once a radio has a full
release. A tag that already exists on that same commit is reused, so an interrupted publish can be run again; an
existing release is never changed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import crucible  # noqa: E402
sys.path.insert(0, str(ROOT / 'tools' / 'rfoundry'))
from rfoundry import boards  # noqa: E402

USAGE_GUIDE = 'docs/kf8sfork/README.md'


def git(*args, check=True):
    out = subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True)
    if check and out.returncode:
        raise ValueError('git ' + ' '.join(args) + ': ' + out.stderr.strip())
    return out.stdout.strip() if out.returncode == 0 else None


class Package:
    """A package directory (build/kf8s/<tag>): its build.json, ZIP and sidecar, checked."""

    def __init__(self, directory):
        self.dir = Path(directory)
        build = self.dir / 'package' / 'build.json'
        if not build.exists():
            raise ValueError(f'{build} not found: give the directory tools/kf8s_build.py printed (build/kf8s/<version>)')
        self.build = json.loads(build.read_text())
        self.tag = self.build['version']
        if crucible.parse_tag(self.tag) is None:
            raise ValueError(f'{self.tag} is not a Crucible version (v0.1.0, v0.1.0-beta.N): an older label, not published here')
        self.prerelease = crucible.is_prerelease(self.tag)
        self.commit = self.build['commit']
        self.zip = self.dir / f'crucible-{self.tag}.zip'
        self.sidecar = self.zip.with_suffix('.zip.sha256')
        for f in (self.zip, self.sidecar):
            if not f.exists():
                raise ValueError(f'{f} not found: the build did not finish')
        if sidecar_ok(self.sidecar.read_text(), self.zip.name, hashlib.sha256(self.zip.read_bytes()).hexdigest()) is False:
            raise ValueError(f'{self.zip.name} does not match {self.sidecar.name}')

    def radios(self):
        ids = sorted({img['board'] for img in self.build.get('images', [])})
        return [boards.BOARDS[i].name if i in boards.BOARDS else i for i in ids]


def sidecar_ok(text, name, digest):
    parts = text.split()
    return len(parts) == 2 and parts[1] == name and parts[0] == digest


def previous_tag(tag, tags):
    """The newest Crucible version below tag, or None."""
    key = crucible.parse_tag(tag)
    older = [t for t in tags if crucible.parse_tag(t) is not None and crucible.parse_tag(t) < key]
    return max(older, key=crucible.parse_tag) if older else None


def changes(since, commit):
    """The first-parent commit subjects (on main: the merged pull requests) since a tag or commit."""
    if since is None:
        return []
    log = git('log', '--first-parent', '--format=%s', f'{since}..{commit}', check=False) or ''
    return [line for line in log.splitlines() if line]


def notes(pkg, project, since, subjects):
    version = pkg.tag.removeprefix('v')
    target = version.split('-')[0]
    lines = [f'{project["name"]} {pkg.tag}: ' + (f'a test build on the way to {target}.' if pkg.prerelease
                                                  else f'release {target}.'), '',
             'Radios: ' + ', '.join(pkg.radios()) + '.',
             f'Based on RadioFoundry {pkg.build.get("upstream", "")[:8]}; Crucible commit {pkg.commit[:8]}.', '']
    if subjects:
        lines += [f'Changes since {since}:'] + [f'- {s}' for s in subjects] + ['']
    lines += ['Validation: the builds and host tests passed. Listening tests on each radio are in the bring-up records;',
              'a radio or feature not listed there is untested.', '']
    if pkg.prerelease:
        lines += ['This is a test version: YSF is incomplete, and settings may change before the release.', '']
    lines += [f'Install or update with the Crucible flasher: {project["flasher"]}',
              f'Usage and testing guide: {project["repo"]}/blob/main/{USAGE_GUIDE}',
              f'Help and bug reports: {project["issues"]} (not RadioFoundry).', '',
              'Crucible is based on RadioFoundry (MIT) and is not affiliated with it. The ZIP keeps every license notice;',
              'keep them when sharing it. No AMBE codec is included.']
    return '\n'.join(lines) + '\n'


def draft(pkg, project):
    since = previous_tag(pkg.tag, git('tag', '--list').split())
    if since is None:	# the first version: since the last dated test label
        since = git('describe', '--tags', '--abbrev=0', '--match', 'crucible-*', pkg.commit, check=False)
    return notes(pkg, project, since, changes(since, pkg.commit))


def publish(pkg, project, notes_file, dry_run):
    repo = project['repo'].removeprefix('https://github.com/')
    git('fetch', 'origin', 'main')
    if git('merge-base', '--is-ancestor', pkg.commit, 'origin/main', check=False) is None:
        raise ValueError(f'{pkg.commit[:8]} is not on origin/main: merge and push main first')
    if subprocess.run(['gh', 'release', 'view', pkg.tag, '--repo', repo], capture_output=True).returncode == 0:
        raise ValueError(f'release {pkg.tag} already exists on {repo}: never changed here')
    local = git('rev-list', '-n1', f'refs/tags/{pkg.tag}', check=False)
    remote = git('ls-remote', 'origin', f'refs/tags/{pkg.tag}^{{}}', check=False) or ''
    remote = remote.split()[0] if remote else None
    for where, sha in (('here', local), ('on origin', remote)):
        if sha is not None and sha != pkg.commit:
            raise ValueError(f'tag {pkg.tag} {where} is on {sha[:8]}, not the package commit {pkg.commit[:8]}')
    steps = []
    if local is None:
        steps.append(['git', '-C', str(ROOT), 'tag', '-a', pkg.tag, '-m', f'{project["name"]} {pkg.tag}', pkg.commit])
    if remote is None:
        steps.append(['git', '-C', str(ROOT), 'push', 'origin', f'refs/tags/{pkg.tag}'])
    steps.append(['gh', 'release', 'create', pkg.tag, str(pkg.zip), str(pkg.sidecar), '--repo', repo, '--verify-tag',
                  '--title', f'{project["name"]} {pkg.tag}', '--notes-file', str(notes_file)]
                 + (['--prerelease'] if pkg.prerelease else ['--latest']))
    for step in steps:
        print('+ ' + ' '.join(step), flush=True)
        if not dry_run:
            subprocess.run(step, check=True)
    if not dry_run:
        print(f'published {pkg.tag}' + (' (pre-release)' if pkg.prerelease else '') +
              '. Rebuild and deploy the flasher site so it lists it (CRUCIBLE.md "Flasher page").')


def self_test():
    assert sidecar_ok('ab  crucible-v0.1.0.zip\n', 'crucible-v0.1.0.zip', 'ab')
    assert sidecar_ok('ab  x.zip\n', 'crucible-v0.1.0.zip', 'ab') is False
    assert sidecar_ok('ac  crucible-v0.1.0.zip\n', 'crucible-v0.1.0.zip', 'ab') is False
    tags = ['v0.1.0-beta.1', 'v0.1.0-beta.2', 'crucible-1-2-261127-1', 'v26.11', 'v0.1.0']
    assert previous_tag('v0.1.0-beta.3', tags) == 'v0.1.0-beta.2'
    assert previous_tag('v0.1.1-beta.1', tags) == 'v0.1.0'
    assert previous_tag('v0.1.0-beta.1', tags) is None

    class Fake:
        tag, prerelease, commit = 'v0.1.0-beta.2', True, '0123456789abcdef'
        build = {'upstream': 'fedcba9876543210'}

        def radios(self):
            return ['A', 'B']
    project = {'name': 'Crucible', 'flasher': 'https://f.invalid/', 'repo': 'https://github.com/o/r',
               'issues': 'https://github.com/o/r/issues'}
    text = notes(Fake(), project, 'v0.1.0-beta.1', ['Merge pull request #1 from o/x'])
    assert text.startswith('Crucible v0.1.0-beta.2: a test build on the way to 0.1.0.')
    assert '- Merge pull request #1 from o/x' in text and 'Changes since v0.1.0-beta.1:' in text
    assert 'RadioFoundry fedcba98' in text and 'test version' in text and 'not affiliated' in text
    Fake.tag, Fake.prerelease = 'v0.1.0', False
    text = notes(Fake(), project, None, [])
    assert text.startswith('Crucible v0.1.0: release 0.1.0.') and 'Changes since' not in text and 'test version' not in text
    print('crucible release: package, tag and notes checks passed')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--self-test', action='store_true')
    ap.add_argument('action', nargs='?', choices=('notes', 'publish'))
    ap.add_argument('package', nargs='?', help='build/kf8s/<version>')
    ap.add_argument('--notes', help='the release notes (publish): edit the draft that "notes" prints')
    ap.add_argument('--dry-run', action='store_true', help='check and print the commands, change nothing')
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.action or not args.package:
        ap.error('give an action and a package directory')
    project = crucible.config(ROOT)['project']
    pkg = Package(args.package)
    if args.action == 'notes':
        sys.stdout.write(draft(pkg, project))
        return
    if not args.notes:
        ap.error('publish needs --notes FILE: write the draft with "notes", read and edit it first')
    publish(pkg, project, Path(args.notes).resolve(), args.dry_run)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit('crucible release: ' + str(error))
