#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 David Behnke
"""Build Crucible's flasher page: one static folder for tinker.wc8mi.net/crucible/ (CRUCIBLE.md, "Flasher page").

  python3 tools/crucible_site.py [--out build/crucible-site] [--package ZIP ...] [--no-github]

It builds the shared site (site/build.py), keeps its flasher page and the files that page loads, gives it Crucible's
shell and look (site/crucible/), and turns on its two Crucible switches: updates come from prebuilt releases
(releases/index.json, release-index/1) and nothing is reported to RadioFoundry's services. The releases are
wc8mi/crucible's GitHub releases (through the gh CLI: the repository may be private) and any --package ZIP, each
checked against its .zip.sha256 sidecar and its SHA256SUMS before a byte is copied. Only crucible.toml's radios are
offered. Run it with site/requirements.txt available, as site/build.py says (uv run --with-requirements ...).
"""
import argparse
import datetime
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
import urllib.parse
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import crucible  # noqa: E402

FORMAT = 'release-index/1'
SAFE = re.compile(r'^[A-Za-z0-9._-]+$')

# The single-cut mark (docs/crucible/brand/mark-single-cut.svg) drawn in the band's iron
MARK = ('<svg viewBox="0 0 70 80" aria-hidden="true" focusable="false"><path fill="currentColor" fill-rule="evenodd" '
        'd="M9 0H22V18H55Q65 18 65 28V34H70V47H65V70Q65 80 55 80H14Q4 80 4 70V49H0V35H4V26Q4 21 9 19Z M15 44H25Q23 36 '
        '38 26Q36 33 42 36Q47 38 46 44H55L49 65Q47 70 41 70H29Q22 70 20 64Z"/></svg>')
# Light and dark: a disc half filled
THEME_ICON = ('<svg viewBox="0 0 20 20" aria-hidden="true" focusable="false"><circle cx="10" cy="10" r="7.5" fill="none" '
              'stroke="currentColor" stroke-width="1.8"/><path d="M10 2.5a7.5 7.5 0 0 1 0 15z" fill="currentColor"/></svg>')

# The update page's words in ASD-STE100 (ADR 001, "Words"): one term for one thing, imperative steps, CAUTION and
# STOP alerts. They replace the shared page's English after RadioFoundry is renamed; each key must be in the shared
# catalog with the same {placeholders} (self_test)
WORDS = {
    'flash.release.title': 'Your radio',
    'flash.release.lead': 'The installer reads the radio and finds the correct update.',
    'flash.release.first_install': "This page cannot install Crucible on a radio with the manufacturer's firmware. "
                                   'Install RadioFoundry first.',
    'flash.release.running': 'On the radio now',
    'flash.release.choose': 'Select a version',
    'flash.release.installed': 'On your radio',
    'flash.release.test': 'Test release',
    'flash.release.notes': 'What changes in {name}',
    'flash.release.older_title': 'CAUTION: This version is older than the firmware on your radio.',
    'flash.release.older_text': 'Make a backup first. An older version can erase settings from a newer version. It can also make AES transmit keys unusable.',
    'flash.release.older_ok': 'I have a backup and I accept the risk',
    'flash.release.older_first': 'Select the checkbox for the older version first.',
    'flash.release.unknown_age': 'The radio does not show the date of its firmware. The installer cannot tell if this version is older.',
    'flash.release.none': 'STOP: No version is available for the {radio}.',
    'flash.release.show_tests': 'Show test releases',
    'flash.release.show_tests_text': 'Test releases have less testing.',
    'flash.release.loading': 'Wait',
    'flash.release.go': 'Update to {name}',
    'flash.release.reinstall': 'Update again',
    'flash.release.missing': 'STOP: The firmware file is not on this site (HTTP {status}).',
    'flash.release.bootloader': 'This radio cannot get the update through the cable now. Do the steps below to put the radio in update mode.',
    'flash.update.step_on': 'Set the radio to ON. Do not hold a key.',
    'flash.update.step_cable': 'Connect the programming cable to the radio and to the computer.',
    'flash.update.step_choose': 'Click Connect and select the radio in the list.',
    'flash.update.not_rfoundry': 'STOP: The radio does not have RadioFoundry or Crucible. You cannot update it. Install '
                                 'RadioFoundry first.',
    'flash.update.backup': 'Make a backup first',
    'flash.update.backup_text': 'Optional. An update keeps the channels, settings, calibration and voice codec.',
    'flash.write.bootloader_title': 'Put the radio in update mode',
    'flash.write.then_connect': 'Click Connect.',
    'flash.write.found': 'The radio is in update mode.',
    'flash.write.title': 'Update',
    'flash.write.go': 'Update',
    'flash.write.writing': 'Do not disconnect the cable',
    'flash.write.finished': 'The update is complete. The radio starts again.',
    'flash.write.failed': 'STOP: The update stopped ({error}). Put the radio in update mode and do the update again.',
    'flash.write.unexpected': 'STOP: The radio reports “{what}”. This is not the radio you selected.',
    'flash.done.title': 'Check the radio',
    'flash.done.lead': 'When the radio shows its screen again, click Check the radio.',
    'flash.done.lead_noscreen': 'When the radio starts again, click Check the radio.',
    'flash.done.check': 'Check the radio',
    # The Check step and the backup after the update. No "codeplug", "stock" or Codeplug Editor (the Crucible site has
    # no editor)
    'flash.done.pick_port': 'The radio now has Crucible. The browser shows it as a new device. Click Check the radio '
                            'again and select {radio} in the list. If the list is empty, wait until the radio starts.',
    'flash.done.no_codec': 'STOP: The radio has no voice codec. DMR voice is not available. FM operates. Report the '
                           'problem.',
    'flash.backup.title_after': 'Make a backup of the radio',
    'flash.backup.opt.all_text': 'Firmware, voice codec, calibration, channels and all other data in the memory chip.',
    'flash.backup.opt.firmware_text': 'The firmware, the voice codec and the factory calibration.',
    'flash.backup.opt.codeplug': 'Channels and calibration',
    'flash.backup.opt.codeplug_text': 'Channels, contacts, zones, settings and the factory calibration.',
    'flash.backup.opt.calibration_text': 'Only the factory calibration. This is the tuning data of the radio.',
    'flash.backup.run': 'Make the backup',
    'flash.backup.saved': 'The backup is saved as “{name}” ({size} KB). Keep it in a safe place.',
    'flash.nav.finish_no_backup': 'Finish without a backup',
    'report.something_wrong': 'Is there a problem?',
    'flash.c7.check.title': 'Back up and check the radio',
    'flash.c7.check.run': 'Back up and check',
    'flash.done.ready': 'Crucible operates. DMR voice is available.',
    'flash.step.update': 'Find',
    'flash.step.write': 'Update',
    'flash.step.done': 'Check',
    'flash.step.c7_done': 'Check',
    'flash.f4.title': 'Update',
    'flash.f4.go': 'Update',
    'flash.c7.write.title': 'Update',
    'flash.c7.write.go': 'Update',
    'flash.stay_on_page': 'CAUTION: Keep this page open and the computer screen on until this step is complete.',
    'flash.start_over': 'Start again',
    'flash.step.check': 'Backup',
    'flash.codec.title': 'Firmware file',
    'flash.codec.lead': "Crucible uses the voice codec from the manufacturer's firmware file. The file stays on your computer.",
    'flash.codec.action_title': "Select the manufacturer's firmware file",
    'flash.codec.stock_title': "The manufacturer's firmware file",
    'flash.codec.stock_pick': 'Select the file',
    'flash.codec.ready_title': 'The installer accepts this file',
    'flash.codec.mode.merge': "Use the voice codec from the manufacturer's firmware file",
    'flash.codec.why.unverified_stock': 'the radio cannot show its voice codec',
    'flash.codec.need_key': 'The installer uses this file to open update mode.',
    'flash.codec.need_codec': 'Crucible uses the voice codec from this file.',
    'flash.codec.stock_files': 'Files the installer accepts: {files}.',
    'flash.codec.key_saved': 'This browser keeps the update key of the radio.',
    'flash.codec.image': 'Crucible: {what}, {size} bytes.',
    'flash.codec.why_not.unknown': "The installer cannot read the voice codec of a radio with the manufacturer's firmware.",
}

# The update-mode steps of a radio in the words of ADR 001 (site/crucible/radios/<id>/update-mode.toml): they replace
# the shared ones (radios.json flash.bootloader), shown when an update must go through update mode
def update_mode():
    return {f.parent.name: tomllib.loads(f.read_text())['steps']
            for f in sorted((ROOT / 'site' / 'crucible' / 'radios').glob('*/update-mode.toml'))}

# A first install is RadioFoundry's (ADR 001, revision 2026-11-27): a plain link to its flasher page, never a request
RF_FLASH = 'https://radiofoundry.org/flash/index.html'

# Crucible sends nothing to RadioFoundry or any other service: the browser allows requests and scripts from this
# site only (inline scripts: the theme and terms lines in the head), whatever a shared module tries
CSP = ('<meta http-equiv="Content-Security-Policy" content="default-src \'self\'; connect-src \'self\'; '
       'script-src \'self\' \'unsafe-inline\'; style-src \'self\' \'unsafe-inline\'; img-src \'self\' data:; '
       'font-src \'self\'; object-src \'none\'; base-uri \'self\'; form-action \'none\'">')
# The shared flasher's build service; the copy Crucible serves has none (no_upstream_api)
UPSTREAM_API = 'const API = "https://api.radiofoundry.org/v1/";'

# The update page sends a visitor who has not accepted the terms on this computer to the front page (ADR 001)
TERMS_FIRST = '<script>try{if(localStorage.getItem("crucible-terms")!=="1")location.replace("./#terms")}catch(e){}</script>'


class SiteError(Exception):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def sidecar_digest(text, name):
    """The digest a .sha256 sidecar gives for name: 'HEX  name' or a bare 'HEX'."""
    for line in text.splitlines():
        parts = line.split()
        if parts and re.fullmatch(r'[0-9a-f]{64}', parts[0]) and (len(parts) == 1 or parts[-1].lstrip('*') == name):
            return parts[0]
    raise SiteError(f'{name}: its .sha256 sidecar names no digest for it')


def read_package(zip_path, digest, boards):
    """The images of one release package: [(board, options, member name, manifest name, manifest dict, bytes...)]."""
    data = Path(zip_path).read_bytes()
    if sha256(data) != digest:
        raise SiteError(f'{Path(zip_path).name}: SHA-256 does not match its sidecar')
    with zipfile.ZipFile(zip_path) as z:
        names = {Path(n).name: n for n in z.namelist() if not n.endswith('/')}
        def member(name):
            if name not in names:
                raise SiteError(f'{Path(zip_path).name}: {name} missing')
            return z.read(names[name])
        build = json.loads(member('build.json'))
        sums = {}
        for line in member('SHA256SUMS').decode().splitlines():
            if line.strip():
                hexd, name = line.split(None, 1)
                sums[name.strip().lstrip('*')] = hexd
        images = []
        for entry in build.get('images', []):
            if entry.get('board') not in boards:
                continue
            files = {}
            for key in ('image', 'manifest'):
                name = entry[key]
                if not SAFE.match(name):
                    raise SiteError(f'{Path(zip_path).name}: unsafe file name {name!r}')
                blob = member(name)
                if sums.get(name) != sha256(blob):
                    raise SiteError(f'{Path(zip_path).name}: {name} does not match SHA256SUMS')
                files[key] = (name, blob)
            manifest = json.loads(files['manifest'][1])
            if manifest.get('board') != entry['board'] or manifest.get('image', {}).get('size') != len(files['image'][1]):
                raise SiteError(f'{Path(zip_path).name}: {files["manifest"][0]} does not describe its image')
            images.append({'board': entry['board'], 'options': dict(entry.get('options') or {}), 'files': files,
                           'manifest': manifest})
    return build, images


def github_releases(repo, cache):
    """wc8mi/crucible's releases with a crucible-*.zip and its sidecar, downloaded into cache."""
    out = subprocess.run(['gh', 'api', '--paginate', f'repos/{repo}/releases'], capture_output=True, text=True)
    if out.returncode:
        raise SiteError('gh api: ' + (out.stderr.strip() or 'failed'))
    releases = []
    for r in json.loads(out.stdout or '[]'):
        if r.get('draft'):
            continue
        assets = {a['name']: a for a in r.get('assets', [])}
        zips = [n for n in assets if n.startswith('crucible-') and n.endswith('.zip') and n + '.sha256' in assets]
        if not zips:
            continue
        tag = r['tag_name']
        folder = cache / tag
        folder.mkdir(parents=True, exist_ok=True)
        name = zips[0]
        if not (folder / name).exists() or not (folder / (name + '.sha256')).exists():
            subprocess.run(['gh', 'release', 'download', tag, '--repo', repo, '--clobber', '--dir', str(folder),
                            '--pattern', name, '--pattern', name + '.sha256'], check=True)
        releases.append({'tag': tag, 'name': r.get('name') or tag, 'prerelease': bool(r.get('prerelease')),
                         'published': r.get('published_at') or '', 'url': r.get('html_url') or '',
                         'notes': r.get('body') or '', 'zip': folder / name})
    return releases


def local_release(zip_path):
    path = Path(zip_path)
    tag = path.name.removesuffix('.zip')
    tag = tag.removeprefix('crucible-') if tag.startswith(('crucible-crucible-', 'crucible-v')) else tag
    # build.json says whether a Crucible version is a test release (crucible.toml [release] prerelease); the old
    # dated labels always were
    prerelease = True
    if crucible.parse_tag(tag) is not None:
        try:
            with zipfile.ZipFile(path) as z:
                prerelease = bool(json.loads(z.read('build.json')).get('prerelease', True))
        except (OSError, KeyError, ValueError, zipfile.BadZipFile):
            pass
    return {'tag': tag, 'name': tag, 'prerelease': prerelease, 'published': '', 'url': '', 'notes': '', 'zip': path}


def write_releases(releases, out, boards, repo):
    """Copy each release's images for Crucible's radios under out/releases/<tag>/ and write the index."""
    rdir = out / 'releases'
    rdir.mkdir(parents=True, exist_ok=True)
    index = {'format': FORMAT, 'repo': repo, 'releases': []}
    for rel in releases:
        if not SAFE.match(rel['tag']):
            raise SiteError(f'unsafe tag {rel["tag"]!r}')
        side = Path(str(rel['zip']) + '.sha256')
        digest = sidecar_digest(side.read_text(), rel['zip'].name)
        build, images = read_package(rel['zip'], digest, boards)
        if not images:
            continue
        folder = rdir / rel['tag']
        folder.mkdir(exist_ok=True)
        entries = []
        for img in images:
            for name, blob in img['files'].values():
                (folder / name).write_bytes(blob)
            m = img['manifest']
            entries.append({'board': img['board'], 'options': img['options'],
                            'image': f'{rel["tag"]}/{img["files"]["image"][0]}',
                            'manifest': f'{rel["tag"]}/{img["files"]["manifest"][0]}',
                            'git': m.get('git', ''), 'git_time': m.get('git_time', 0), 'size': m['image']['size']})
        index['releases'].append({k: rel[k] for k in ('tag', 'name', 'prerelease', 'published', 'url', 'notes')}
                                 | {'version': build.get('version', ''), 'images': entries})
    (rdir / 'index.json').write_text(json.dumps(index, indent=1) + '\n')
    return index


def one(pattern, text, what, flags=re.S):
    found = re.findall(pattern, text, flags)
    if len(found) != 1:
        raise SiteError(f'flash page: expected one {what}, found {len(found)} (the shared page changed?)')
    return re.search(pattern, text, flags)


def words(catalog):
    """WORDS, each checked against the shared catalog: the key is there and keeps its {placeholders}."""
    for k, v in WORDS.items():
        if k not in catalog:
            raise SiteError(f'flash page: {k} is not in the shared catalog (renamed upstream?)')
        if set(re.findall(r'\{\w+\}', v)) != set(re.findall(r'\{\w+\}', catalog[k])):
            raise SiteError(f'flash page: {k} changes the placeholders of "{catalog[k]}"')
    return WORDS


def catalog_words(page, project, extra=None):
    """The page's string catalog with Crucible's name, plus extra (each key checked against the catalog)."""
    i18n = one(r'(<script type="application/json" id="i18n">)(.*?)(</script>)', page, 'string catalog')
    strings = json.loads(i18n.group(2))
    strings = {k: v.replace('RadioFoundry', project['name']) for k, v in strings.items()}
    if extra is not None:
        strings.update(extra(strings))
    catalog = json.dumps(strings, ensure_ascii=False).replace('</', '<\\/')
    return page[:i18n.start(2)] + catalog + page[i18n.end(2):]


def shell(page, project, assets, built, root, title, body, help_more=''):
    """A shared site page in Crucible's head, band and foot. root leads from the page to the site root ('' or
    '../../'); body is the new body tag; help_more follows the help line in the foot."""
    name = html.escape(project['name'])
    head = (f'<head>\n<meta charset="utf-8">\n{CSP}\n<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            f'<title>{title}</title>\n<meta name="description" content="{html.escape(project["tagline"])}">\n'
            f'<meta name="theme-color" content="#ff7738">\n<meta name="robots" content="noindex">\n'
            f'<link rel="icon" href="{root}crucible/icon-32.png?v={assets}" type="image/png" sizes="32x32">\n'
            f'<link rel="apple-touch-icon" href="{root}crucible/icon-180.png?v={assets}">\n'
            f'<link rel="preload" href="{root}crucible/fonts/barlow-condensed-700-latin.woff2" as="font" type="font/woff2" crossorigin>\n'
            f'<link rel="stylesheet" href="{root}static/css/tokens.css?v={assets}">\n'
            f'<link rel="stylesheet" href="{root}static/css/site.css?v={assets}">\n'
            f'<link rel="stylesheet" href="{root}crucible/crucible.css?v={assets}">\n'
            '<script>try{var t=localStorage.getItem("rfoundry-theme");if(t)document.documentElement.dataset.theme=t}catch(e){}</script>\n'
            f'{TERMS_FIRST.replace("./#terms", (root or "./") + "#terms")}\n'
            '</head>')
    m = one(r'<head>.*?</head>', page, 'head')
    page = page[:m.start()] + head + page[m.end():]
    m = one(r'<body [^>]*>', page, 'body tag')
    page = page[:m.start()] + body + page[m.end():]
    band = ('<header class="cx-band">\n\t<div class="wrap">\n'
            f'\t\t<a class="cx-brand" href="{root or "./"}">{MARK}<span class="cx-word">{name}</span></a>\n'
            f'\t\t<button class="theme-toggle" type="button" aria-label="Switch light and dark" title="Switch light and dark">{THEME_ICON}</button>\n'
            '\t</div>\n</header>')
    m = one(r'<header class="site-header">.*?</header>', page, 'site header')
    page = page[:m.start()] + band + page[m.end():]
    repo, issues, upstream, community = (html.escape(project[k]) for k in ('repo', 'issues', 'upstream', 'community'))
    foot = ('<footer class="cx-foot">\n\t<div class="wrap">\n'
            f'\t\t<p><strong>Help:</strong> ask in the <a href="{community}">Crucible Community on Discord</a>, or <a href="{issues}">open an issue on {html.escape(project["repo"].removeprefix("https://github.com/"))}</a>. '
            f'{help_more}This page sends nothing to a server.</p>\n'
            f'\t\t<p>{name} is maintained by {html.escape(project["maintainer"])}. It is based on '
            f'<a href="{upstream}">RadioFoundry</a> (MIT) and is not affiliated with it. Do not ask RadioFoundry about {name}. '
            f'<a href="{repo}">Source</a>.</p>\n'
            f'\t\t<p class="build-stamp">{html.escape(built)}</p>\n'
            '\t</div>\n</footer>')
    m = one(r'<footer class="site-footer">.*?</footer>', page, 'site footer')
    return page[:m.start()] + foot + page[m.end():]


def crucible_page(flash, project, assets, built):
    """The shared flasher page with Crucible's head, shell, words and switches."""
    page = catalog_words(flash, project, words)
    name = html.escape(project['name'])
    page = shell(page, project, assets, built, '', f'{name} · Update',
                 '<body class="page-flash" data-root="" data-home="" data-gl-more="More" '
                 'data-releases="releases/index.json" data-reporting="off">',
                 'Report a Problem saves a file. Attach the file to the issue. ')
    lead = 'Your voice codec, channels and settings stay on the radio.'
    m = one(r'(<section class="wrap section page-head">\s*<h1>)(.*?)(</h1>\s*<p class="lead">)(.*?)(</p>)', page, 'page head')
    first = (f'\n<p class="notice info cx-first">{name} updates only a radio with RadioFoundry or {name}. For a radio with the '
             f'manufacturer\'s firmware, <a href="{RF_FLASH}">install RadioFoundry</a> first.</p>')
    page = page[:m.start()] + m.group(1) + f'Update {name}' + m.group(3) + lead + m.group(5) + first + page[m.end():]
    m = one(r'<dialog id="beta-notice".*?</dialog>\s*', page, 'beta notice')
    page = page[:m.start()] + page[m.end():]
    page = page.replace('"../static/', '"static/')
    if '../' in re.sub(r'<script type="application/json" id="i18n">.*?</script>', '', page, flags=re.S):
        raise SiteError('flash page: a ../ link is left')
    return page


# The satellite page: the AMSAT bulletin and PE0SAT's amateur list come through this site's proxies
# (/crucible/api/amsat-tle and /crucible/api/pe0sat-tle on the tinker host), named relative to the page so the folder
# can move; the frequencies are the page's own table (satellite-tle.js)
SAT_PAGE = 'tools/satellite/index.html'
SAT_API = '../../api/amsat-tle'
SAT_PE0SAT_API = '../../api/pe0sat-tle'
SAT_LEAD = ('Load new satellite data on a radio with RadioFoundry or {name}. A firmware update is not necessary. '
            'The radio must have the satellite feature. Select up to 10 FM satellites. Send them to the radio over USB, '
            'or save a bundle file.')


def satellite_page(tle, project, assets, built):
    """The shared Satellite TLE page (site/templates/satellite_tle.html) in Crucible's shell."""
    page = catalog_words(tle, project)
    name = html.escape(project['name'])
    page = shell(page, project, assets, built, '../../', f'{name} · Satellites',
                 '<body class="page-tools" data-root="../../" data-home="../../" data-gl-more="More">',
                 "The Load latest TLEs button gets the AMSAT and PE0SAT lists through this site. ")
    m = one(r'(<section class="wrap section page-head">)\s*<p class="eyebrow">.*?</p>(\s*<h1>)(.*?)(</h1>\s*<p class="lead">)(.*?)(</p>)',
            page, 'satellite page head')
    page = (page[:m.start()] + m.group(1) + m.group(2) + 'Satellites' + m.group(4) + SAT_LEAD.format(name=name)
            + m.group(6) + page[m.end():])
    m = one(r'<section class="wrap section satellite-tle" id="satellite-tle"', page, 'satellite form')
    page = page[:m.end()] + f' data-amsat-api="{SAT_API}" data-pe0sat-api="{SAT_PE0SAT_API}"' + page[m.end():]
    return page


def local_links(out):
    """Every href and src in the served pages that names a local file names one that is there."""
    bad = []
    pages = [(f, re.sub(r'<script type="application/json" id="i18n">.*?</script>', '', f.read_text(), flags=re.S))
             for f in sorted(out.rglob('*.html'))]
    feed = out / 'data' / 'news.json'
    if feed.exists():		# the posts More adds go into index.html
        pages.append((out / 'index.html', '\n'.join(x['html'] for x in json.loads(feed.read_text())['posts'])))
    for f, text in pages:
        for ref in re.findall(r'(?:href|src)="([^"]*)"', text):
            if not ref or re.match(r'^(?:[a-z][a-z0-9+.-]*:|//|#)', ref, re.I):
                continue
            target = (f.parent / ref.split('#')[0].split('?')[0])
            if target.is_dir():
                target = target / 'index.html'
            if not target.resolve().is_relative_to(out.resolve()) or not target.exists():
                bad.append(f'{f.relative_to(out)}: {ref}')
    if bad:
        raise SiteError('a page links to a missing local file: ' + ', '.join(bad))


def no_upstream_api(out):
    """Take the build service's address out of the served flasher, then fail if any RadioFoundry service is left."""
    app = out / 'static' / 'js' / 'flasher' / 'app.js'
    src = app.read_text()
    if src.count(UPSTREAM_API) != 1:
        raise SiteError('flash page: the API line of app.js changed (the shared page changed?)')
    app.write_text(src.replace(UPSTREAM_API, 'const API = "";	// Crucible: no build service (tools/crucible_site.py)'))
    site = out / 'data' / 'site.json'
    if site.exists() and (json.loads(site.read_text()).get('reports') or {}).get('sentry_dsn'):
        raise SiteError('data/site.json: error reporting is on (reports.sentry_dsn)')
    for f in out.rglob('*'):
        if f.suffix in ('.js', '.html', '.json') and re.search(r'api\.radiofoundry\.org', f.read_text(errors='replace')):
            raise SiteError(f'{f.relative_to(out)}: names a RadioFoundry service')


# News: one Markdown post per update in site/crucible/news/ (CRUCIBLE.md, "News"), newest first on the front page
NEWS_FORMAT = 'crucible-news/1'
NEWS_FIRST = 5			# posts in the page itself; More loads the next NEWS_FIRST from data/news.json
NEWS_NAME = re.compile(r'^(\d{4}-\d{2}-\d{2})-([a-z0-9]+(?:-[a-z0-9]+)*)\.md$')
# {{boards/<id>}} in a post is that radio's name (radio_names): a post names a radio without a radio id in its text
NEWS_BOARD = re.compile(r'\{\{boards/([a-z0-9_]+)\}\}')
# ADR 001's words: no banned term (titles, the front page, the update page)
BANNED = r"\b(flash|bootloader|DFU|stock|codeplug|beta|upgrade|OEM)\b|n't"


def radio_names(radios):
    """{board id: 'Maker Model'} for the offered radios, as the front page's radio list names them."""
    out = {}
    for r in radios:
        maker = r.get('maker', '')
        model = r.get('model') or r['name'].removeprefix(maker + ' ')
        out.setdefault(r.get('flash', {}).get('board') or r['id'], f'{maker} {model}'.strip())
    return out


def parse_post(name, text, names=None):
    """One post file: the SPDX comment, a front matter (date, title, optional release and seq) and a Markdown body.
    {id, date, title, release, seq, body}; seq orders posts of one day (higher is newer). names (radio_names) fills
    {{boards/<id>}}; an id not in it fails."""
    m = NEWS_NAME.match(name)
    if not m:
        raise SiteError(f'news/{name}: name it YYYY-MM-DD-slug.md (lowercase, digits, hyphens)')
    rest = re.sub(r'^(\s*<!--.*?-->)*\s*', '', text, flags=re.S)
    fm = re.match(r'---\n(.*?)\n---\n', rest, re.S)
    if not fm:
        raise SiteError(f'news/{name}: no front matter between --- lines')
    meta = {}
    for line in fm.group(1).splitlines():
        if line.strip():
            k, sep, v = line.partition(':')
            if not sep or k.strip() not in ('date', 'title', 'release', 'seq'):
                raise SiteError(f'news/{name}: front matter line {line!r} (date, title, release, seq)')
            meta[k.strip()] = v.strip()
    date, slug = m.groups()
    def radio(t):
        if (names or {}).get(t.group(1)) is None:
            raise SiteError(f'news/{name}: {t.group(0)} is not a Crucible radio')
        return names[t.group(1)]
    meta = {k: NEWS_BOARD.sub(radio, v) for k, v in meta.items()}
    if meta.get('date') != date:
        raise SiteError(f'news/{name}: date {meta.get("date")!r} differs from the file name')
    try:
        datetime.date.fromisoformat(date)
    except ValueError:
        raise SiteError(f'news/{name}: {date} is not a date')
    title, release = meta.get('title', ''), meta.get('release', '')
    if not title or len(title.split()) > 10:
        raise SiteError(f'news/{name}: a title of 1 to 10 words')
    if re.search(BANNED, title, re.I):
        raise SiteError(f'news/{name}: the title uses a word ADR 001 does not allow')
    if release and not SAFE.match(release):
        raise SiteError(f'news/{name}: unsafe release tag {release!r}')
    body = NEWS_BOARD.sub(radio, rest[fm.end():]).strip()
    if not body:
        raise SiteError(f'news/{name}: no text')
    return {'id': f'news-{date}-{slug}', 'date': date, 'title': title, 'release': release,
            'seq': int(meta.get('seq') or 0), 'body': body}


def read_news(folder, names=None):
    """Every post in folder, newest first; ids unique."""
    posts = [parse_post(f.name, f.read_text(), names) for f in sorted(Path(folder).glob('*.md'))]
    ids = [x['id'] for x in posts]
    if len(set(ids)) != len(ids):
        raise SiteError('news: two posts with one name')
    return sorted(posts, key=lambda x: (x['date'], x['seq'], x['id']), reverse=True)


def news_date(date):
    d = datetime.date.fromisoformat(date)
    return f'{d.day} {d.strftime("%B")} {d.year}'


def news_html(posts, repo, render):
    """Each post as an <article>: render(markdown) gives the body's HTML (raw HTML in a post is escaped)."""
    out = []
    for x in posts:
        rel = (f'\n\t<p class="news-rel">Release <a href="{html.escape(repo)}/releases/tag/{x["release"]}">'
               f'<code>{x["release"]}</code></a></p>') if x['release'] else ''
        out.append(f'<article class="news-post" id="{x["id"]}">\n\t<p class="news-date"><time datetime="{x["date"]}">'
                   f'{news_date(x["date"])}</time></p>\n\t<h3><a href="#{x["id"]}">{html.escape(x["title"])}</a></h3>\n'
                   f'\t<div class="news-body">{render(x["body"])}</div>{rel}\n</article>')
    return out


def markdown_renderer():
    from markdown_it import MarkdownIt		# site/requirements.txt
    md = MarkdownIt('commonmark', {'html': False, 'linkify': False})
    return md.render


def news_atom(posts, items, project):
    """An Atom feed of the posts (news.xml), their HTML as content."""
    base = project['flasher'].rstrip('/') + '/'
    esc = lambda v: html.escape(v, quote=True)
    updated = (posts[0]['date'] if posts else '2026-01-01') + 'T00:00:00Z'
    def absolute(item):		# a feed reader has no page to be relative to: every link in a post is made absolute
        return re.sub(r'((?:href|src)=")([^"]*)"',
                      lambda m: m.group(1) + html.escape(urllib.parse.urljoin(base, html.unescape(m.group(2)))) + '"', item)
    entries = ''.join(
        f'<entry><id>{esc(base)}#{x["id"]}</id><title>{esc(x["title"])}</title><updated>{x["date"]}T00:00:00Z</updated>'
        f'<link href="{esc(base)}#{x["id"]}"/><content type="html">{esc(absolute(item))}</content></entry>\n'
        for x, item in zip(posts, items))
    return (f'<?xml version="1.0" encoding="utf-8"?>\n<feed xmlns="http://www.w3.org/2005/Atom">\n'
            f'<id>{esc(base)}news.xml</id><title>{esc(project["name"])} news</title><updated>{updated}</updated>\n'
            f'<link href="{esc(base)}"/><link rel="self" href="{esc(base)}news.xml"/>'
            f'<author><name>{esc(project["maintainer"])}</name></author>\n{entries}</feed>\n')


def front_page(template, project, radios, assets, built, news='', news_more=''):
    """Crucible's front page (site/crucible/installer/page.html): the browser gate, the terms, the Update door and the
    news (the newest posts as HTML; news_more the More button's markup, or empty when every post is shown)."""
    items = []
    for r in sorted(radios, key=lambda r: r.get('order', 0)):
        maker, model = r.get('maker', ''), r.get('model') or r['name'].removeprefix(r.get('maker', '') + ' ')
        also = ', '.join(a.removeprefix(maker + ' ') for a in r.get('also', []))
        small = html.escape(maker + (f' · also {also}' if also else ''))
        items.append(f'\t\t\t<li>{html.escape(model)} <small>{small}</small></li>')
    fill = {k: html.escape(project[k]) for k in ('name', 'tagline', 'repo', 'issues', 'upstream', 'maintainer', 'community')}
    fill.update(csp=CSP, rf_flash=RF_FLASH, news=news, news_more=news_more, assets=html.escape(assets), built=html.escape(built), radios='\n'.join(items),
                repo_slug=html.escape(project['repo'].removeprefix('https://github.com/')))
    page = re.sub(r'\{\{(\w+)\}\}', lambda m: fill[m.group(1)], template)
    if not items:
        raise SiteError('front page: no radios')
    return page


def build(out, packages, github, site_out=None):
    cfg = crucible.config(ROOT)
    project = cfg['project']
    boards = crucible.board_ids(ROOT)
    repo_slug = project['repo'].removeprefix('https://github.com/')
    with tempfile.TemporaryDirectory() as tmp:
        site = Path(site_out) if site_out else Path(tmp) / 'site'
        if not site_out:
            subprocess.run([sys.executable, str(ROOT / 'site' / 'build.py'), '--out', str(site),
                            '--repo-url', project['repo']], check=True, stdout=subprocess.DEVNULL)
        if out.exists():
            shutil.rmtree(out)
        out.mkdir(parents=True)
        shutil.copytree(site / 'static', out / 'static')
        (out / 'data').mkdir()
        for name in ('features.json', 'variants.json', 'flash-boards.json', 'f4-stub.json', 'c7000-stub.json', 'releases.json'):
            shutil.copy2(site / 'data' / name, out / 'data' / name)
        radios = json.loads((site / 'data' / 'radios.json').read_text())
        radios['radios'] = [r for r in radios['radios'] if r.get('id') in boards or r.get('flash', {}).get('board') in boards]
        radios['planned'] = []
        modes = update_mode()
        for r in radios['radios']:		# the update-mode steps in Crucible's words replace the shared ones
            mode = modes.get(r.get('id'))
            if mode:
                r['flash']['bootloader'] = mode
        (out / 'data' / 'radios.json').write_text(json.dumps(radios))
        info = json.loads((site / 'data' / 'site.json').read_text())
        commit = subprocess.run(['git', 'rev-parse', '--short=8', 'HEAD'], cwd=ROOT, capture_output=True,
                                text=True).stdout.strip()
        info.update({'version': f'crucible-{commit}', 'branch': '', 'repo': project['repo'], 'community': '', 'release': '', 'beta': '', 'reports': {'sentry_dsn': ''}})
        info.pop('tool', None)
        (out / 'data' / 'site.json').write_text(json.dumps(info, indent=1))
        shutil.copytree(ROOT / 'site' / 'crucible', out / 'crucible', ignore=shutil.ignore_patterns('page.html', 'radios', 'news'))
        brand = ROOT / 'docs' / 'crucible' / 'brand'
        for size in (32, 180, 512):
            shutil.copy2(brand / f'hot-core-icon-{size}.png', out / 'crucible' / f'icon-{size}.png')
        flash = (site / 'flash' / 'index.html').read_text()
        assets = re.search(r'\?v=([0-9a-f]+)', flash).group(1)
        built = f'{project["name"]} {commit} · page built {info.get("built", "")}'
        (out / 'update.html').write_text(crucible_page(flash, project, assets, built))
        (out / SAT_PAGE).parent.mkdir(parents=True)
        (out / SAT_PAGE).write_text(satellite_page((site / SAT_PAGE).read_text(), project, assets, built))
        no_upstream_api(out)
        template = (ROOT / 'site' / 'crucible' / 'installer' / 'page.html').read_text()
        posts = read_news(ROOT / 'site' / 'crucible' / 'news', radio_names(radios['radios']))
        items = news_html(posts, project['repo'], markdown_renderer())
        feed = {'format': NEWS_FORMAT, 'first': NEWS_FIRST,
                'posts': [{k: x[k] for k in ('id', 'date', 'title', 'release')} | {'html': item}
                          for x, item in zip(posts, items)]}
        (out / 'data' / 'news.json').write_text(json.dumps(feed, ensure_ascii=False, indent=1) + '\n')
        (out / 'news.xml').write_text(news_atom(posts, items, project))
        more = ('<button class="btn quiet" type="button" id="news-more" data-feed="data/news.json">More news</button>'
                if len(items) > NEWS_FIRST else '')
        (out / 'index.html').write_text(front_page(template, project, radios['radios'], assets, built,
                                                   '\n'.join(items[:NEWS_FIRST]), more))
    releases = [local_release(p) for p in packages]
    if github:
        releases += github_releases(repo_slug, ROOT / 'build' / 'crucible-releases')
    index = write_releases(releases, out, set(boards), repo_slug)
    after_update(out)
    local_links(out)
    return index


def after_update(out):
    """The flasher's offer after a satellite-capable install (`${HOME}tools/satellite/index.html`, from update.html
    with data-home="") lands on the page this build made."""
    for js in ('app.js', 'c7000ui.js'):
        if '`${HOME}' + SAT_PAGE + '`' not in (out / 'static' / 'js' / 'flasher' / js).read_text():
            raise SiteError(f'{js}: the satellite offer changed (the shared page changed?)')
    if 'data-home=""' not in (out / 'update.html').read_text() or not (out / SAT_PAGE).is_file():
        raise SiteError(f'update.html: the satellite offer does not reach {SAT_PAGE}')


def self_test():
    assert sidecar_digest('a' * 64 + '  x.zip\n', 'x.zip') == 'a' * 64
    assert sidecar_digest('b' * 64 + '\n', 'x.zip') == 'b' * 64
    try:
        sidecar_digest('c' * 64 + '  y.zip\n', 'x.zip')
    except SiteError:
        pass
    else:
        raise AssertionError('sidecar for another file accepted')
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        man = {'board': 'b1', 'git': 'abc1234', 'git_time': 5, 'image': {'size': 3}}
        files = {'i.bin': b'\x01\x02\x03', 'i.json': json.dumps(man).encode()}
        files['build.json'] = json.dumps({'version': 'v', 'images': [
            {'board': 'b1', 'options': {'POWER': '5w'}, 'image': 'i.bin', 'manifest': 'i.json'},
            {'board': 'other', 'options': {}, 'image': 'i.bin', 'manifest': 'i.json'}]}).encode()
        files['SHA256SUMS'] = ''.join(f'{sha256(files[n])}  {n}\n' for n in ('i.bin', 'i.json')).encode()
        zpath = d / 'crucible-t-1.zip'
        with zipfile.ZipFile(zpath, 'w') as z:
            for n, b in files.items():
                z.writestr(n, b)
        digest = sha256(zpath.read_bytes())
        Path(str(zpath) + '.sha256').write_text(f'{digest}  {zpath.name}\n')
        out = d / 'out'
        index = write_releases([local_release(zpath)], out, {'b1'}, 'o/r')
        assert index['releases'][0]['tag'] == 'crucible-t-1'
        assert local_release('x/crucible-v0.1.0.zip') | {'zip': None} == {'tag': 'v0.1.0', 'name': 'v0.1.0', 'prerelease': True,
                                                                       'published': '', 'url': '', 'notes': '', 'zip': None}
        for flag in (True, False):
            vzip = d / f'crucible-v0.1.{int(flag)}.zip'
            with zipfile.ZipFile(vzip, 'w') as z:
                z.writestr('build.json', json.dumps({'version': vzip.stem[9:], 'prerelease': flag}))
            assert local_release(vzip)['tag'] == vzip.stem[9:] and local_release(vzip)['prerelease'] is flag
        assert [i['board'] for i in index['releases'][0]['images']] == ['b1'], 'only Crucible boards'
        assert (out / 'releases' / 'crucible-t-1' / 'i.bin').read_bytes() == b'\x01\x02\x03'
        assert index['releases'][0]['images'][0]['options'] == {'POWER': '5w'}
        try:
            read_package(zpath, '0' * 64, {'b1'})
        except SiteError:
            pass
        else:
            raise AssertionError('wrong zip digest accepted')
        bad = d / 'bad.zip'
        files['i.bin'] = b'\x09\x02\x03'
        with zipfile.ZipFile(bad, 'w') as z:
            for n, b in files.items():
                z.writestr(n, b)
        try:
            read_package(bad, sha256(bad.read_bytes()), {'b1'})
        except SiteError:
            pass
        else:
            raise AssertionError('image not matching SHA256SUMS accepted')
    flash = ('<html><head><title>x</title></head><body class="page-flash" data-root="../" data-home="../">'
             '<header class="site-header">nav</header><main><section class="wrap section page-head">\n<h1>Install</h1>\n'
             '<p class="lead">l</p></section><dialog id="beta-notice">b</dialog>\n<link href="../static/css/x.css?v=1">'
             '<div id="flasher"></div></main><footer class="site-footer">f</footer>'
             '<script type="application/json" id="i18n">' + json.dumps({'a': 'Runs RadioFoundry', **{k: v for k, v in
             json.loads((ROOT / 'site' / 'i18n' / 'en.json').read_text()).items() if k in WORDS}}) + '</script>'
             '<script type="module" src="../static/js/flasher/app.js?v=1"></script></body></html>')
    proj = {'name': 'Crucible', 'tagline': 't', 'repo': 'https://github.com/o/r', 'issues': 'https://github.com/o/r/issues',
            'upstream': 'https://github.com/u/u', 'maintainer': 'M', 'community': 'https://discord.gg/c'}
    page = crucible_page(flash, proj, '1', 'v')
    assert 'data-releases="releases/index.json"' in page and 'data-reporting="off"' in page
    assert '"Your radio"' in page and '<h1>Update Crucible</h1>' in page and 'Runs Crucible' in page and 'beta-notice' not in page and 'site-header' not in page
    assert '"static/js/flasher/app.js' in page and '../' not in page and 'not affiliated' in page
    assert 'href="https://discord.gg/c">Crucible Community on Discord</a>' in page, 'the update page links the Discord community'
    assert CSP in page and "connect-src 'self'" in CSP, 'the update page allows requests to this site only'
    with tempfile.TemporaryDirectory() as d:
        js = Path(d) / 'static' / 'js' / 'flasher'
        js.mkdir(parents=True)
        (js / 'app.js').write_text(f'x\n{UPSTREAM_API}\ny\n')
        no_upstream_api(Path(d))
        assert 'radiofoundry' not in (js / 'app.js').read_text()
        (Path(d) / 'data.json').write_text('"https://api.radiofoundry.org/v1/samples"')
        try:
            no_upstream_api(Path(d))
        except SiteError:
            pass
        else:
            raise AssertionError('a RadioFoundry service left in the output')
    assert 'location.replace("./#terms")' in page, 'the update page asks for the terms first'
    # the satellite page: the shared template in Crucible's shell; the TLE lists come through this site's proxies only
    tle = ('<html><head><title>x</title></head><body class="page-tools" data-root="../../" data-home="../../">'
           '<header class="site-header">nav</header><main><section class="wrap section page-head">\n'
           '<p class="eyebrow"><a href="../../tools/index.html">Tools</a></p>\n<h1>Satellite TLE Update</h1>\n<p class="lead">l</p>'
           '</section><section class="wrap section satellite-tle" id="satellite-tle"><button id="tle-latest">a</button></section>'
           '</main><footer class="site-footer">f</footer><script type="application/json" id="i18n">{"b": "RadioFoundry"}</script>'
           '<script type="module" src="../../static/js/tletool.js?v=1"></script></body></html>')
    sat = satellite_page(tle, proj, '1', 'v')
    assert '<h1>Satellites</h1>' in sat and 'eyebrow' not in sat and 'tools/index.html' not in sat and 'site-header' not in sat
    assert f'id="satellite-tle" data-amsat-api="{SAT_API}" data-pe0sat-api="{SAT_PE0SAT_API}"' in sat
    assert not any(a.startswith(('/', 'http')) for a in (SAT_API, SAT_PE0SAT_API))
    assert CSP in sat and 'amsat.org' not in CSP and 'pe0sat' not in CSP and 'location.replace("../../#terms")' in sat and '"Crucible"' in sat
    assert 'href="../../crucible/crucible.css?v=1"' in sat and 'class="cx-brand" href="../../"' in sat
    assert all(len(x.split()) <= 25 for x in re.split(r'(?<=\.) ', SAT_LEAD))
    with tempfile.TemporaryDirectory() as d:
        out = Path(d)
        (out / 'a').mkdir()
        (out / 'a' / 'x.css').write_text('')
        (out / 'index.html').write_text('<a href="a/x.css?v=1">x</a><a href="#n">n</a><a href="https://e/">e</a><a href="a/">d</a>'
                                        '<script type="application/json" id="i18n">{"k": "<a href=\\"gone.html\\">"}</script>')
        try:
            local_links(out)
        except SiteError as e:
            assert 'a/' in str(e) and 'x.css' not in str(e) and 'gone' not in str(e)
        else:
            raise AssertionError('a link to a folder without index.html passed')
        (out / 'a' / 'index.html').write_text('<a href="../index.html">up</a>')
        local_links(out)
        (out / 'data').mkdir()
        (out / 'data' / 'news.json').write_text(json.dumps({'posts': [{'html': '<a href="gone.html">g</a>'}]}))
        try:
            local_links(out)
        except SiteError as e:
            assert 'gone.html' in str(e)
        else:
            raise AssertionError('a missing link in a post only in news.json passed')
        (out / 'data' / 'news.json').write_text(json.dumps({'posts': [{'html': '<a href="a/">a</a>'}]}))
        local_links(out)
        js = out / 'static' / 'js' / 'flasher'
        js.mkdir(parents=True)
        for f in ('app.js', 'c7000ui.js'):
            (js / f).write_text('location.assign(`${HOME}' + SAT_PAGE + '`);')
        (out / 'update.html').write_text('<body data-home="">')
        try:
            after_update(out)
        except SiteError:
            pass
        else:
            raise AssertionError('the satellite offer passed without its page')
        (out / SAT_PAGE).parent.mkdir(parents=True)
        (out / SAT_PAGE).write_text('')
        after_update(out)
        (out / 'b.html').write_text('<a href="../outside.html">o</a>')
        try:
            local_links(out)
        except SiteError:
            pass
        else:
            raise AssertionError('a link out of the site passed')
    shared = json.loads((ROOT / 'site' / 'i18n' / 'en.json').read_text())
    assert words(shared) is WORDS, 'every Crucible word replaces a shared key with the same placeholders'
    for bad in ({'flash.release.go': 'x'}, {}):
        try:
            words({**{k: shared[k] for k in WORDS if k in shared}, **bad} if bad else {})
        except SiteError:
            pass
        else:
            raise AssertionError('a lost key or placeholder accepted')
    # ADR 001's words: no banned term, at most 20 words for a step, 25 for anything else (per sentence)
    banned = BANNED
    def sentences(text):
        return [x for x in re.split(r'(?<=[.?!])\s+', re.sub(r'<[^>]+>|\{\{\w+\}\}', 'X', text)) if x.strip()]
    offered = crucible.board_ids(ROOT)
    modes = update_mode()
    assert modes, 'no update-mode words'
    for rid, steps in modes.items():
        assert (ROOT / 'boards' / rid / 'board.toml').is_file() and rid in offered, f'update mode {rid}: not a Crucible board'
        for v in steps:
            assert not re.search(banned, v, re.I) and len(v.split()) <= 20, f'update mode {rid}: {v}'
    # Crucible never installs on a radio with the manufacturer's firmware (ADR 001, revision 2026-11-27): the update
    # page has no install card and names RadioFoundry's flasher page for a first install, as a plain link
    assert 'data-card' not in page and 'install-card' not in page, 'no install card on the update page'
    assert f'<a href="{RF_FLASH}">install RadioFoundry</a> first' in page, 'the update page sends a first install to RadioFoundry'
    tpl_real = (ROOT / 'site' / 'crucible' / 'installer' / 'page.html').read_text()
    assert 'update.html#install' not in tpl_real and 'data-screen="install"' not in tpl_real, 'the front page has no Install door'
    assert '{{rf_flash}}' in tpl_real, 'the front page links RadioFoundry for a first install'
    home = re.search(r'<section data-screen="home".*?</section>', tpl_real, re.S).group(0)
    for para in re.findall(r'<(?:p|li|h2)[^>]*>(.*?)</(?:p|li|h2)>', home, re.S):
        for x in sentences(para):
            assert not re.search(banned, x, re.I) and len(x.split()) <= 25, f'front page: {x}'
    assert f'href="{SAT_PAGE}"' in home, 'the front page links the satellite page'
    assert not re.search(banned, SAT_LEAD, re.I)
    for v in WORDS.values():
        assert not re.search(banned, v, re.I), v
        assert all(len(x.split()) <= 25 for x in sentences(v)), f'too long: {v}'
    tpl = '{{csp}}<a href="{{rf_flash}}">R</a><h1>{{name}}</h1><ul>\n{{radios}}\n</ul><p>{{built}}</p><a href="{{issues}}">{{repo_slug}}</a><a href="{{community}}">D</a>'
    radios = [{'name': 'TYT X-2', 'maker': 'TYT', 'model': 'X-2', 'also': ['TYT X-3', 'Other Y'], 'order': 2},
              {'name': 'B Z-1', 'maker': 'B', 'model': 'Z-1', 'also': [], 'order': 1}]
    front = front_page(tpl, proj, radios, '1', 'v <b>')
    assert front.startswith(CSP) and '{{' not in front and '<h1>Crucible</h1>' in front and 'v &lt;b&gt;' in front
    assert front.index('Z-1') < front.index('X-2'), 'radios in their order'
    assert '<li>X-2 <small>TYT · also X-3, Other Y</small></li>' in front and '>o/r</a>' in front
    assert '<a href="https://discord.gg/c">D</a>' in front, 'the front page links the Discord community'
    assert f'<a href="{RF_FLASH}">R</a>' in front and RF_FLASH.startswith('https://radiofoundry.org/'), 'a plain link'
    # news: the repository's posts parse, newest first with unique anchors; a bad file is refused; raw HTML is escaped
    posts = read_news(ROOT / 'site' / 'crucible' / 'news', {b: b for b in crucible.board_ids(ROOT)})
    assert posts and [(x['date'], x['seq']) for x in posts] == sorted(((x['date'], x['seq']) for x in posts), reverse=True)
    assert len({x['id'] for x in posts}) == len(posts)
    head = '<!-- SPDX-' 'License-Identifier: MIT -->\n<!-- c -->\n'  # split so REUSE doesn't read it as this file's
    good = parse_post('2026-01-02-a-b.md', head + '---\ndate: 2026-01-02\ntitle: Hello\nrelease: crucible-x-1\n---\nText.\n')
    assert good['id'] == 'news-2026-01-02-a-b' and good['release'] == 'crucible-x-1' and good['body'] == 'Text.'
    for name, text in (('2026-01-02-A.md', '---\ndate: 2026-01-02\ntitle: T\n---\nx'),
                       ('2026-02-30-a.md', '---\ndate: 2026-02-30\ntitle: T\n---\nx'),
                       ('2026-01-02-a.md', '---\ndate: 2026-01-03\ntitle: T\n---\nx'),
                       ('2026-01-02-a.md', '---\ndate: 2026-01-02\ntitle: ' + 'w ' * 11 + '\n---\nx'),
                       ('2026-01-02-a.md', '---\ndate: 2026-01-02\ntitle: T\nrelease: a/b\n---\nx'),
                       ('2026-01-02-a.md', '---\ndate: 2026-01-02\ntitle: T\nauthor: x\n---\nx'),
                       ('2026-01-02-a.md', 'date: 2026-01-02\ntitle: T\nx'),
                       ('2026-01-02-a.md', '---\ndate: 2026-01-02\ntitle: A beta build\n---\nx'),
                       ('2026-01-02-a.md', '---\ndate: 2026-01-02\ntitle: T\n---\nThe {{boards/nope}} radio.')):
        try:
            parse_post(name, text)
        except SiteError:
            pass
        else:
            raise AssertionError(f'bad post accepted: {name} {text!r}')
    item = news_html([good], 'https://github.com/o/r', lambda b: '<p>' + html.escape(b) + '</p>')[0]
    assert 'id="news-2026-01-02-a-b"' in item and 'href="#news-2026-01-02-a-b"' in item and '2 January 2026' in item
    assert 'href="https://github.com/o/r/releases/tag/crucible-x-1"' in item
    try:
        render = markdown_renderer()
    except ImportError:
        render = None			# rendering is checked where site/requirements.txt is installed
    if render:
        assert '&lt;script' in render('<script>x</script>') and 'href="javascript' not in render('[a](javascript:alert(1))')
    feed = news_atom([good], [item], {**proj, 'flasher': 'https://h/c/'})
    assert '<id>https://h/c/#news-2026-01-02-a-b</id>' in feed and '&lt;article' in feed
    assert 'href=&quot;https://h/c/#news-2026-01-02-a-b&quot;' in feed and 'href=&quot;#' not in feed, 'feed links are absolute'
    # {{boards/<id>}}: the radio's name, in the title (words counted after) and the body
    names = radio_names([{'id': 'r1', 'maker': 'Acme', 'model': 'X-1', 'name': 'Acme X-1'},
                         {'id': 'r2x', 'flash': {'board': 'r2'}, 'maker': 'Acme', 'name': 'Acme Y 2'}])
    assert names == {'r1': 'Acme X-1', 'r2': 'Acme Y 2'}
    named = parse_post('2026-01-02-n.md', '---\ndate: 2026-01-02\ntitle: The {{boards/r2}} joins\n---\nOn the {{boards/r1}}.', names)
    assert named['title'] == 'The Acme Y 2 joins' and named['body'] == 'On the Acme X-1.'
    try:
        parse_post('2026-01-02-n.md', '---\ndate: 2026-01-02\ntitle: {{boards/r2}} ' + 'w ' * 8 + '\n---\nx', names)
    except SiteError:
        pass
    else:
        raise AssertionError('a title over 10 words after the radio name passed')
    front = front_page('{{news}}|{{news_more}}', proj, radios, '1', 'v', item, '<button>')
    assert front == item + '|<button>'
    print('crucible site: package checks, index, page shell, front page, news checks passed')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', default=str(ROOT / 'build' / 'crucible-site'))
    ap.add_argument('--package', action='append', default=[], help='a release ZIP (with its .zip.sha256 beside it) to offer')
    ap.add_argument('--no-github', action='store_true', help="don't fetch wc8mi/crucible's GitHub releases")
    ap.add_argument('--site', help='a site/build.py output to use instead of building one')
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return
    index = build(Path(args.out), args.package, not args.no_github, args.site)
    n = sum(len(r['images']) for r in index['releases'])
    print(f'crucible site: {args.out} ({len(index["releases"])} releases, {n} images)')


if __name__ == '__main__':
    try:
        main()
    except (SiteError, subprocess.CalledProcessError, OSError) as e:
        raise SystemExit(f'crucible site: {e}')
