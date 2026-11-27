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

# The update-mode steps of each radio in the words of ADR 001: they replace the shared ones (radios.json
# flash.bootloader), shown when an update must go through update mode. The self-test checks the words and lengths
UPDATE_MODE = {
    'dm1701': ['Turn the volume knob to OFF.', 'Connect the cable to the radio and to the computer.',
               'Push and hold PTT and SK1. SK1 is the key above PTT.', 'Turn the volume knob to ON.',
               'Release the keys. The screen stays dark. This is correct.'],
    # SK1's place as board.toml words it (issue #203)
    'mduv380': ['Turn the volume knob to OFF.', 'Connect the cable to the radio and to the computer.',
                'Push and hold PTT and SK1. SK1 is the side key just above PTT.', 'Turn the volume knob to ON.',
                'Release the keys. The screen stays dark. This is correct.'],
    # the factory update mode of the HR_C7000 radios, the way in when the update through the firmware fails
    'db25d': ['Set the radio to OFF.', 'Connect the cable to the radio and to the computer.', 'Push and hold P1.',
              'Push the power key.', 'Release P1. The red LED is on and the screen stays blank. This is correct.'],
}

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
    tag = tag.removeprefix('crucible-') if tag.startswith('crucible-crucible-') else tag
    return {'tag': tag, 'name': tag, 'prerelease': True, 'published': '', 'url': '', 'notes': '', 'zip': path}


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


def crucible_page(flash, project, assets, built):
    """The shared flasher page with Crucible's head, shell, words and switches."""
    page = flash
    i18n = one(r'(<script type="application/json" id="i18n">)(.*?)(</script>)', page, 'string catalog')
    strings = json.loads(i18n.group(2))
    strings = {k: v.replace('RadioFoundry', project['name']) for k, v in strings.items()}
    strings.update(words(strings))
    catalog = json.dumps(strings, ensure_ascii=False).replace('</', '<\\/')
    page = page[:i18n.start(2)] + catalog + page[i18n.end(2):]

    name = html.escape(project['name'])
    title = f'{name} · Update'
    lead = 'Your voice codec, channels and settings stay on the radio.'
    head = (f'<head>\n<meta charset="utf-8">\n{CSP}\n<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            f'<title>{title}</title>\n<meta name="description" content="{html.escape(project["tagline"])}">\n'
            f'<meta name="theme-color" content="#ff7738">\n<meta name="robots" content="noindex">\n'
            f'<link rel="icon" href="crucible/icon-32.png?v={assets}" type="image/png" sizes="32x32">\n'
            f'<link rel="apple-touch-icon" href="crucible/icon-180.png?v={assets}">\n'
            f'<link rel="preload" href="crucible/fonts/barlow-condensed-700-latin.woff2" as="font" type="font/woff2" crossorigin>\n'
            f'<link rel="stylesheet" href="static/css/tokens.css?v={assets}">\n'
            f'<link rel="stylesheet" href="static/css/site.css?v={assets}">\n'
            f'<link rel="stylesheet" href="crucible/crucible.css?v={assets}">\n'
            '<script>try{var t=localStorage.getItem("rfoundry-theme");if(t)document.documentElement.dataset.theme=t}catch(e){}</script>\n'
            f'{TERMS_FIRST}\n'
            '</head>')
    m = one(r'<head>.*?</head>', page, 'head')
    page = page[:m.start()] + head + page[m.end():]
    m = one(r'<body [^>]*>', page, 'body tag')
    page = page[:m.start()] + ('<body class="page-flash" data-root="" data-home="" data-gl-more="More" '
                               'data-releases="releases/index.json" data-reporting="off">') + page[m.end():]
    band = ('<header class="cx-band">\n\t<div class="wrap">\n'
            f'\t\t<a class="cx-brand" href="./">{MARK}<span class="cx-word">{name}</span></a>\n'
            f'\t\t<button class="theme-toggle" type="button" aria-label="Switch light and dark" title="Switch light and dark">{THEME_ICON}</button>\n'
            '\t</div>\n</header>')
    m = one(r'<header class="site-header">.*?</header>', page, 'site header')
    page = page[:m.start()] + band + page[m.end():]
    m = one(r'(<section class="wrap section page-head">\s*<h1>)(.*?)(</h1>\s*<p class="lead">)(.*?)(</p>)', page, 'page head')
    first = (f'\n<p class="cx-first">{name} updates only a radio with RadioFoundry or {name}. For a radio with the '
             f'manufacturer\'s firmware, <a href="{RF_FLASH}">install RadioFoundry</a> first.</p>')
    page = page[:m.start()] + m.group(1) + f'Update {name}' + m.group(3) + lead + m.group(5) + first + page[m.end():]
    m = one(r'<dialog id="beta-notice".*?</dialog>\s*', page, 'beta notice')
    page = page[:m.start()] + page[m.end():]
    repo, issues, upstream, community = (html.escape(project[k]) for k in ('repo', 'issues', 'upstream', 'community'))
    foot = ('<footer class="cx-foot">\n\t<div class="wrap">\n'
            f'\t\t<p><strong>Help:</strong> ask in the <a href="{community}">Crucible Community on Discord</a>, or <a href="{issues}">open an issue on {html.escape(project["repo"].removeprefix("https://github.com/"))}</a>. '
            'Report a Problem saves a file. Attach the file to the issue. This page sends nothing to a server.</p>\n'
            f'\t\t<p>{name} is maintained by {html.escape(project["maintainer"])}. It is based on '
            f'<a href="{upstream}">RadioFoundry</a> (MIT) and is not affiliated with it. Do not ask RadioFoundry about {name}. '
            f'<a href="{repo}">Source</a>.</p>\n'
            f'\t\t<p class="build-stamp">{html.escape(built)}</p>\n'
            '\t</div>\n</footer>')
    m = one(r'<footer class="site-footer">.*?</footer>', page, 'site footer')
    page = page[:m.start()] + foot + page[m.end():]
    page = page.replace('"../static/', '"static/')
    if '../' in re.sub(r'<script type="application/json" id="i18n">.*?</script>', '', page, flags=re.S):
        raise SiteError('flash page: a ../ link is left')
    return page


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


def front_page(template, project, radios, assets, built):
    """Crucible's front page (site/crucible/installer/page.html): the browser gate, the terms and the two doors."""
    items = []
    for r in sorted(radios, key=lambda r: r.get('order', 0)):
        maker, model = r.get('maker', ''), r.get('model') or r['name'].removeprefix(r.get('maker', '') + ' ')
        also = ', '.join(a.removeprefix(maker + ' ') for a in r.get('also', []))
        small = html.escape(maker + (f' · also {also}' if also else ''))
        items.append(f'\t\t\t<li>{html.escape(model)} <small>{small}</small></li>')
    fill = {k: html.escape(project[k]) for k in ('name', 'tagline', 'repo', 'issues', 'upstream', 'maintainer', 'community')}
    fill.update(csp=CSP, rf_flash=RF_FLASH, assets=html.escape(assets), built=html.escape(built), radios='\n'.join(items),
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
        for r in radios['radios']:		# the update-mode steps in Crucible's words replace the shared ones
            mode = UPDATE_MODE.get(r.get('id'))
            if mode:
                r['flash']['bootloader'] = mode
        (out / 'data' / 'radios.json').write_text(json.dumps(radios))
        info = json.loads((site / 'data' / 'site.json').read_text())
        commit = subprocess.run(['git', 'rev-parse', '--short=8', 'HEAD'], cwd=ROOT, capture_output=True,
                                text=True).stdout.strip()
        info.update({'version': f'crucible-{commit}', 'branch': '', 'repo': project['repo'], 'community': '', 'release': '', 'beta': '', 'reports': {'sentry_dsn': ''}})
        info.pop('tool', None)
        (out / 'data' / 'site.json').write_text(json.dumps(info, indent=1))
        shutil.copytree(ROOT / 'site' / 'crucible', out / 'crucible', ignore=shutil.ignore_patterns('page.html'))
        brand = ROOT / 'docs' / 'crucible' / 'brand'
        for size in (32, 180, 512):
            shutil.copy2(brand / f'hot-core-icon-{size}.png', out / 'crucible' / f'icon-{size}.png')
        flash = (site / 'flash' / 'index.html').read_text()
        assets = re.search(r'\?v=([0-9a-f]+)', flash).group(1)
        built = f'{project["name"]} {commit} · page built {info.get("built", "")}'
        (out / 'update.html').write_text(crucible_page(flash, project, assets, built))
        no_upstream_api(out)
        template = (ROOT / 'site' / 'crucible' / 'installer' / 'page.html').read_text()
        (out / 'index.html').write_text(front_page(template, project, radios['radios'], assets, built))
    releases = [local_release(p) for p in packages]
    if github:
        releases += github_releases(repo_slug, ROOT / 'build' / 'crucible-releases')
    index = write_releases(releases, out, set(boards), repo_slug)
    return index


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
    banned = r"\b(flash|bootloader|DFU|stock|codeplug|beta|upgrade|OEM)\b|n't"
    def sentences(text):
        return [x for x in re.split(r'(?<=[.?!])\s+', re.sub(r'<[^>]+>|\{\{\w+\}\}', 'X', text)) if x.strip()]
    offered = crucible.board_ids(ROOT)
    for rid, steps in UPDATE_MODE.items():
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
    print('crucible site: package checks, index, page shell, front page checks passed')


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
