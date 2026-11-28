#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 David Behnke
"""Build and package a release: Crucible's radios from main, named by semantic version (v0.1.0: crucible.toml
[release] version), or every board from the old kf8sfork and kf8sforkysf branches with a private label.
Other branches refuse this wrapper."""
import argparse
from datetime import datetime
import fcntl
import hashlib
import io
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from urllib.request import urlopen
import zipfile
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'rfoundry'))
from rfoundry import boards, manifest as release_manifest  # noqa: E402
sys.path.insert(0, str(ROOT / 'tools'))
import crucible  # noqa: E402

# These notices match the project's pinned toolchains. Keep the notice pins in sync when changing toolchains.
# ARM 13.3.Rel1 pins Newlib/Nano to this commit. The C-SKY toolchain pins the 4.5.0.20241231 tarball.
NEWLIB_NOTICES = {
    'stm32f4': (
        'https://sourceware.org/git/?p=newlib-cygwin.git;a=blob_plain;f=COPYING.NEWLIB;'
        'hb=1ed15161362b2eb5649b049b7ab0aaf8097bd43a',
        '9a59b4ee3af067045fe0ea78786201c42d54756fe48cbccf44f3dfc398474717',
        None, '9a59b4ee3af067045fe0ea78786201c42d54756fe48cbccf44f3dfc398474717',
    ),
    'hrc7000': (
        'https://sourceware.org/pub/newlib/newlib-4.5.0.20241231.tar.gz',
        '33f12605e0054965996c25c1382b3e463b0af91799001f5bb8c0630f2ec8c852',
        'newlib-4.5.0.20241231/COPYING.NEWLIB',
        'fcfb5ec69b6ab52676dcc4dab7cf4338c8000ef97812dadd35b8592a640a8419',
    ),
}


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


# The branches allowed a private label, and the label's prefix: Crucible releases come from main; the older KF8S and
# YSF experiment labels stay for their branches
BRANCH_PREFIX = {'main': 'crucible', 'kf8sfork': 'kf8s', 'kf8sforkysf': 'kf8sysf'}


def upstream_remote():
    """The remote that is RadioFoundry (crucible.toml upstream); origin when nothing matches."""
    url = crucible.config(ROOT).get('project', {}).get('upstream', '') if crucible.active(ROOT) else ''
    want = url.rstrip('/').removesuffix('.git').lower()
    for line in git('remote', '-v').splitlines():
        name, remote_url = line.split()[:2]
        if want and remote_url.rstrip('/').removesuffix('.git').lower().replace('git@github.com:', 'https://github.com/') == want:
            return name
    return 'origin'


def require_branch(branch):
    if branch not in BRANCH_PREFIX:
        raise ValueError('private version labels require branch ' + ' or '.join(BRANCH_PREFIX))
    return BRANCH_PREFIX[branch]


def next_sequence(path, day):
    """Reserve once per complete build attempt, shared across worktrees and protected against concurrent builders."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.with_suffix('.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        saved = json.loads(path.read_text()) if path.exists() else {}
        sequence = saved.get('sequence', 0) + 1 if saved.get('day') == day else 1
        descriptor, temporary = tempfile.mkstemp(prefix=path.name, dir=path.parent)
        try:
            with os.fdopen(descriptor, 'w') as stream:
                json.dump({'day': day, 'sequence': sequence}, stream)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, path)
        finally:
            Path(temporary).unlink(missing_ok=True)
        return sequence



def release_tags(remote):
    """Every tag name, local and on remote, so a label already pushed from another checkout is never reused."""
    names = set(git('tag', '--list').split())
    out = subprocess.run(['git', '-C', str(ROOT), 'ls-remote', '--tags', '--refs', remote], capture_output=True, text=True)
    if out.returncode:
        raise ValueError(f'cannot list the tags on {remote}: ' + out.stderr.strip())
    names |= {line.split('refs/tags/', 1)[1] for line in out.stdout.splitlines() if 'refs/tags/' in line}
    return names


def crucible_label(version, tags):
    """The tag a Crucible build gets: v<version>. Refuses a version already tagged, here or on origin: crucible.toml
    [release] version must be raised first."""
    release = f'v{version}'
    if release in tags:
        raise ValueError(f'{release} is already tagged: raise crucible.toml [release] version (CRUCIBLE.md "Versions")')
    return release


# The C-SKY compiler runs under amd64 emulation on an Arm host, where cc1 now and then dies of a segfault on a file that
# compiles the next time; make resumes where it stopped, so such a build is run again, ten times at most
COMPILER_CRASH = re.compile(r'internal compiler error: Segmentation fault|^Segmentation fault', re.M)


def run_logged(args, log, env, attempts=1):
    log.write_text('')
    for attempt in range(1, attempts + 1):
        with log.open('a') as stream:
            result = subprocess.run(args, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT)
        if not result.returncode:
            break
        if attempt == attempts or not COMPILER_CRASH.search(log.read_text()):
            raise subprocess.CalledProcessError(result.returncode, args)
        print(f'  the compiler crashed (emulation); building again ({attempt + 1} of {attempts})', flush=True)
    # Docker's note that the image is amd64 on an Arm host is not a build warning
    text = re.sub(r"^WARNING: The requested image's platform .*$", '', log.read_text(), flags=re.M)
    if re.search(r'\bwarning:', text, re.I):
        raise ValueError('build warnings: ' + str(log))


def build_command(board, args):
    if board.platform == 'hrc7000':
        return ['tools/docker/csky-toolchain/run.sh', *args]
    return args


def check_csky_checkout(board, commit, env):
    result = subprocess.run(build_command(board, ['git', 'rev-parse', 'HEAD']),
                            cwd=ROOT, env=env, capture_output=True, text=True)
    if result.returncode or result.stdout.splitlines()[-1:] != [commit]:
        hint = (' For a linked worktree, set RFOUNDRY_THIRD_PARTY to a populated third_party directory '
                'so the toolchain container can mount the common Git directory.') if (ROOT / '.git').is_file() else ''
        raise ValueError('C-SKY toolchain could not read the expected Git commit.' + hint + '\n' +
                         (result.stderr.strip() or result.stdout.strip()))


def configured_features(board, flags, env):
    config = subprocess.check_output(['make', '-s', f'BOARD={board.id}', *flags, 'print-config'],
                                     cwd=ROOT, env=env, text=True)
    return {entry[:-2] for entry in config.split() if entry.endswith('=y')}


def check_built_profile(stamp, flags, commit, version, expected):
    if stamp.split()[:2] != [commit, version]:
        raise ValueError('firmware build Git or release stamp differs from the package')
    compiled = dict(re.findall(r'-DCONFIG_(\w+)=(0|1)(?=\s|$)', flags))
    for feature in ('DMR_AES', 'P25', 'P25_PARROT', 'DMR_IP_GATEWAY'):
        if compiled.get(feature) != ('1' if feature in expected else '0'):
            raise ValueError(f'compiled CONFIG_{feature} differs from print-config')


def check_image_manifest(manifest, expected):
    enabled = set(filter(None, manifest['features'].split(',')))
    if enabled != expected:
        raise ValueError('manifest features differ from print-config: '
                         f'missing {sorted(expected - enabled)}, unexpected {sorted(enabled - expected)}')


def apply_build_info(manifest, stamp):
    preset, _, disabled = stamp.strip().partition(' ')
    if not preset:
        raise ValueError('firmware build did not record its preset')
    if set(disabled.split(',')) != set(manifest['off'].split(',')):
        raise ValueError('manifest off switches differ from the compiled build')
    manifest.update(variant=preset, off=disabled)


def write_release_sums(package, images):
    with (package / 'SHA256SUMS').open('w', encoding='ascii') as sums:
        for entry in images:
            for key in ('image', 'manifest'):
                name = entry[key]
                sums.write(release_manifest.sha256sums_line(name, (package / name).read_bytes()))


def check_archive_images(archive, images):
    missing = {entry[key] for entry in images for key in ('image', 'manifest')} - set(archive.namelist())
    if missing:
        raise ValueError(f'archive is missing image or manifest files: {sorted(missing)}')


def copy_component_notices(source, destination):
    notices = sorted(path for path in source.iterdir() if re.fullmatch(
        r'(?:LICENSES?|NOTICES?|COPYING|COPYRIGHT)(?:[._-].+)?', path.name, flags=re.IGNORECASE))
    if notices:
        destination.mkdir(parents=True, exist_ok=True)
    for path in notices:
        target = destination / path.name
        if path.is_dir():
            shutil.copytree(path, target, dirs_exist_ok=True)
        else:
            shutil.copy2(path, target)
    return bool(notices)


def newlib_notice(platform):
    url, payload_digest, member, notice_digest = NEWLIB_NOTICES[platform]
    with urlopen(url, timeout=30) as response:
        payload = response.read()
    if hashlib.sha256(payload).hexdigest() != payload_digest:
        raise ValueError(f'{platform} Newlib notice download digest mismatch')
    if member is not None:
        with tarfile.open(fileobj=io.BytesIO(payload), mode='r:gz') as archive:
            with archive.extractfile(member) as stream:
                payload = stream.read()
    if hashlib.sha256(payload).hexdigest() != notice_digest:
        raise ValueError(f'{platform} Newlib notice digest mismatch')
    return payload


def copy_firmware_notices(package, images, env):
    platforms = set()
    modules = {}
    imbe = False
    for entry in images:
        board = boards.get(entry['board'])
        platforms.add(board.platform)
        flags = [*entry['build_overrides'], *(f'{key}={value}' for key, value in entry['options'].items())]
        config = subprocess.check_output(['make', '-s', f'BOARD={board.id}', *flags,
                                         'VARS=PLATFORM_3P_SRC INCLUDES CONFIG_IMBE', 'print-vars'],
                                         cwd=ROOT, env=env, text=True)
        values = dict(line.split('=', 1) for line in config.splitlines() if '=' in line)
        imbe |= values['CONFIG_IMBE'] == 'y'
        for name in re.findall(r'third_party/([\w.-]+)(?:/|\s|$)', values['INCLUDES']):
            modules.setdefault(name, set())
        for source in values['PLATFORM_3P_SRC'].split():
            match = re.match(r'third_party/([\w.-]+)/(.+)', source)
            if match:
                modules.setdefault(match[1], set()).add(match[2])

    notices = package / 'notices'
    shared = env.get('RFOUNDRY_THIRD_PARTY')
    for name, sources in sorted(modules.items()):
        source = ROOT / 'third_party' / name
        if shared and (not source.is_dir() or not any(not path.name.startswith('.') for path in source.iterdir())):
            source = Path(shared) / name
        destination = notices / name
        if not source.is_dir() or not copy_component_notices(source, destination):
            raise ValueError(f'compiled component {name} is missing its license files')
        # The FreeRTOS MIT license points to the copyright notices in its source headers.
        if name == 'FreeRTOS-Kernel':
            copyright_lines = set()
            for compiled in sources:
                with (source / compiled).open(encoding='utf-8', errors='replace') as stream:
                    for line in itertools.islice(stream, 16):
                        match = re.match(r'^\s*\*\s+(Copyright (?!notice\b).+)', line)
                        if match:
                            copyright_lines.add(match[1].strip())
            if not copyright_lines:
                raise ValueError('FreeRTOS source copyright notices were not found')
            (destination / 'COPYRIGHT').write_text('\n'.join(sorted(copyright_lines)) + '\n', encoding='utf-8')

    for platform in sorted(platforms):
        source = ROOT / 'platform' / platform
        destination = notices / platform
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / 'THIRD_PARTY', destination / 'THIRD_PARTY')
        copy_component_notices(source, destination)
        if platform in NEWLIB_NOTICES:
            (destination / 'COPYING.NEWLIB').write_bytes(newlib_notice(platform))
    if imbe:
        destination = notices / 'mbelib'
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / 'core' / 'mbe' / 'THIRD_PARTY', destination / 'THIRD_PARTY')
        shutil.copy2(ROOT / 'LICENSES' / 'ISC.txt', destination / 'ISC.txt')


def write_archive(package, archive, images):
    sidecar = archive.with_suffix('.zip.sha256')
    with tempfile.TemporaryDirectory(prefix=f'.{archive.stem}-', dir=archive.parent) as directory:
        temporary = Path(directory) / archive.name
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as zipped:
            for path in sorted(package.rglob('*')):
                if path.is_file():
                    zipped.write(path, path.relative_to(package))
            check_archive_images(zipped, images)
        digest = hashlib.sha256(temporary.read_bytes()).hexdigest()
        checksum = Path(directory) / sidecar.name
        checksum.write_text(f'{digest}  {archive.name}\n', encoding='ascii')
        os.replace(temporary, archive)
        os.replace(checksum, sidecar)
    return digest


def package_readme(version, parrot, without_aes, ysf=False, product=None, prerelease=True):
    aes = ('DMR AES is disabled in this package. AES channels are unavailable and PTT is rejected.\n'
           if without_aes else "DMR AES follows each board's default. See the image manifest.\n")
    diagnostic = ('P25 calibration diagnostic enabled wherever P25 is built. See build.json for overrides.\n'
                  'This temporary profile disables DMR IP gateway, USB networking/PPP, APRS, SFR, hotspot and satellite tracking\n'
                  'to retain two seconds of DC-conditioned PCM in RAM. DMR and AES retain their board defaults.\n'
                  'Open Settings > Advanced > P25 calibration. Press OK on Record speech and speak continuously.\n'
                  'After a 100 ms startup discard it records up to two seconds once, then automatically tests five AGC targets.\n'
                  'Each target replays the same phrase twice for warmup and once for measurement, then decodes all three.\n'
                  'A sixth pass verifies the selected target against the decoded goal of -25 dBFS (+/-1 dB).\n'
                  'Allow about 80 seconds for a full calibration. Report status, Suggested AGC target, all candidate encoder/decoded speech levels,\n'
                  'PCM gain and Limited blocks, plus mic DC and recording/startup rail counts.\n'
                  'An invalid result has no recommendation. The energy gate is not a speech classifier: use clear speech in a quiet room.\n'
                  'The test does not save a new AGC setting. Raw audio is erased after the sweep or on exit; no RF is sent.\n'
                  if parrot else 'The codec diagnostic is off. The DMR IP gateway retains its board defaults.\n')
    return (
        (f'{version}\n{product["name"]} {"pre-release" if prerelease else "release"}, based on RadioFoundry (MIT) and not affiliated with it.\n'
         f'Help and bug reports: {product["issues"]} (not RadioFoundry).\n'
         if product else f'{version}\nPrivate KF8S test build based on RadioFoundry. Contact: dbehnke (KF8S).\n') +
        'Includes transmit and receive audio AGC, dual caller display, and optional P25 caller lookup.\n'
        f'{aes}{diagnostic}'
        + ('YSF TEST: experimental YSF (C4FM DN) receive and transmit, incomplete. DMR and P25 trunking are\n'
           'left out of this build. Report any YSF behavior with the version above.\n'
           if ysf else '') +
        'An upgrade from older settings resets receive AGC to Off and hides the audio levels display.\n'
        'An ambiguous legacy counter blocks AES transmission until recovery. On AES-enabled images, open\n'
        'Settings > Advanced > AES recovery. Choose Keep AES counter, when available, only if you can verify\n'
        'the stored value is this radio\'s latest genuine AES reservation. Otherwise retire every AES transmit\n'
        'key previously used on this radio, install new key material, and then choose Use new AES keys.\n'
        'Recovery saves and restarts. Factory reset preserves the AES counter and recovery state.\n'
        'Never restore older settings backups after AES use or downgrade to firmware that lacks this migration.\n'
        'Enable P25 contact lookup under Settings > Talk & listen (default Off).\n'
        'Match the image and its band/power option to your hardware. See the board reference.\n'
        + (f'Flash with the {product["name"]} flasher, {product["flasher"]}, or use each image with its JSON manifest.\n'
           if product else 'Use each image with its JSON manifest in the RadioFoundry web flasher.\n') +
        'Images contain no AMBE codec. Follow the flasher instructions to preserve or supply your codec.\n'
        'P25 remains unavailable where upstream has not implemented it. See the feature matrix.\n'
        'Keep LICENSE, LICENSE-MIT, CREDITS.md, AUTHORS, CONTRIBUTORS, LICENSES/ and notices/ with redistributed downloads.\n')


def test_release_notices():
    from unittest.mock import patch

    payload = b'complete notice\n'
    member = 'newlib-fixture/COPYING.NEWLIB'
    with io.BytesIO() as stream:
        with tarfile.open(fileobj=stream, mode='w:gz') as archive:
            entry = tarfile.TarInfo(member)
            entry.size = len(payload)
            archive.addfile(entry, io.BytesIO(payload))
        packed = stream.getvalue()
    notice_digest = hashlib.sha256(payload).hexdigest()
    pin = ('https://example.invalid/newlib.tar.gz', hashlib.sha256(packed).hexdigest(), member, notice_digest)
    with patch.dict(NEWLIB_NOTICES, {'fixture': pin}):
        with patch(__name__ + '.urlopen', return_value=io.BytesIO(packed)):
            assert newlib_notice('fixture') == payload
        with patch(__name__ + '.urlopen', return_value=io.BytesIO(b'bad notice')):
            try:
                newlib_notice('fixture')
            except ValueError:
                pass
            else:
                raise AssertionError('a Newlib download with the wrong digest was accepted')
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root / 'component'
        package = root / 'package'
        destination = package / 'notices' / 'component'
        (source / 'LICENSES').mkdir(parents=True)
        for name in ('LICENSE', 'NOTICE.txt', 'LICENSES/MIT.txt', 'unrelated.txt'):
            (source / name).write_text(name + '\n')
        assert copy_component_notices(source, destination)
        archive = root / 'fixture.zip'
        digest = write_archive(package, archive, [])
        with zipfile.ZipFile(archive) as zipped:
            assert set(zipped.namelist()) == {f'notices/component/{name}'
                                           for name in ('LICENSE', 'NOTICE.txt', 'LICENSES/MIT.txt')}
            assert zipped.read('notices/component/LICENSE') == b'LICENSE\n'
        assert hashlib.sha256(archive.read_bytes()).hexdigest() == digest
        assert archive.with_suffix('.zip.sha256').read_text() == f'{digest}  {archive.name}\n'


def self_test():
    assert COMPILER_CRASH.search('csky-elf-gcc: internal compiler error: Segmentation fault signal terminated program cc1')
    assert COMPILER_CRASH.search('x\nSegmentation fault\nmake: *** [Makefile:467: a.tr] Error 1')
    assert not COMPILER_CRASH.search("a.c:3:1: error: expected ';'\nmake: *** [Makefile:467: a.o] Error 1")
    assert require_branch('kf8sfork') == 'kf8s' and require_branch('kf8sforkysf') == 'kf8sysf'
    assert require_branch('main') == 'crucible'
    for branch in ('feat/p25-contact-lookup', 'feat/audio-rx-agc', 'HEAD'):
        try:
            require_branch(branch)
        except ValueError:
            pass
        else:
            raise AssertionError(branch)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'sequence.json'
        assert next_sequence(path, '261123') == 1
        assert next_sequence(path, '261123') == 2
        assert next_sequence(path, '261124') == 1
        from unittest.mock import patch
        with patch('os.replace', side_effect=OSError('interrupted update')):
            try:
                next_sequence(path, '261124')
            except OSError:
                pass
            else:
                raise AssertionError('interruption did not reach the reservation')
        assert next_sequence(path, '261124') == 2
        path.write_text('')
        try:
            next_sequence(path, '261124')
        except ValueError:
            pass
        else:
            raise AssertionError('an empty counter must fail closed')
    assert crucible_label('0.1.0', set()) == 'v0.1.0'
    assert crucible_label('0.1.1', {'v0.1.0', 'v0.1.0-beta.1', 'v26.11'}) == 'v0.1.1'
    try:
        crucible_label('0.1.0', {'v0.1.0'})
    except ValueError:
        pass
    else:
        raise AssertionError('a released version must be raised before the next build')
    standard = {'DMR', 'DMR_AES', 'DMR_IP_GATEWAY', 'P25', 'P25_TX_AGC', 'RX_AGC'}
    diagnostic = (standard - {'DMR_IP_GATEWAY'}) | {'P25_PARROT'}
    without_aes = diagnostic - {'DMR_AES'}
    board_without_aes = {'DMR', 'RX_AGC'}
    for expected in (standard, diagnostic, without_aes, board_without_aes):
        check_image_manifest({'features': ','.join(sorted(expected))}, expected)
    for enabled, expected in ((without_aes, diagnostic), (diagnostic, without_aes),
                              (diagnostic - {'P25_PARROT'}, diagnostic),
                              (diagnostic | {'DMR_IP_GATEWAY'}, diagnostic)):
        try:
            check_image_manifest({'features': ','.join(sorted(enabled))}, expected)
        except ValueError:
            pass
        else:
            raise AssertionError(f'manifest validation accepted {enabled} instead of {expected}')
    commit, tag = '12345678', 'test-version'
    stamp = f'{commit} {tag} 1700000000 fork dbehnke/RadioFoundry'
    flags = '-DCONFIG_DMR_AES=1 -DCONFIG_P25=1 -DCONFIG_P25_PARROT=1 -DCONFIG_DMR_IP_GATEWAY=0'
    check_built_profile(stamp, flags, commit, tag, diagnostic)
    for built_stamp, effective_flags in ((stamp.replace(commit, 'unknown'), flags),
                                          (stamp, flags.replace('DMR_AES=1', 'DMR_AES=0'))):
        try:
            check_built_profile(built_stamp, effective_flags, commit, tag, diagnostic)
        except ValueError:
            pass
        else:
            raise AssertionError('compiled provenance check accepted a mismatched stamp or feature')
    entry = {'image': f'rfoundry-test-{tag}.bin', 'manifest': f'rfoundry-test-{tag}.json'}
    with tempfile.TemporaryDirectory() as directory:
        package = Path(directory)
        image = package / entry['image']
        image.write_bytes(b'fixture')
        manifest = {'variant': 'full', 'off': 'VOICE_CAPTURE,DMR_IP_GATEWAY'}
        apply_build_info(manifest, 'custom DMR_IP_GATEWAY,VOICE_CAPTURE\n')
        assert manifest == {'variant': 'custom', 'off': 'DMR_IP_GATEWAY,VOICE_CAPTURE'}
        try:
            apply_build_info(manifest, 'custom DMR_IP_GATEWAY\n')
        except ValueError:
            pass
        else:
            raise AssertionError('manifest accepted an off list that differs from the firmware')
        manifest_path = package / entry['manifest']
        manifest_path.write_text(release_manifest.dumps(manifest), encoding='utf-8')
        write_release_sums(package, [entry])
        sums = (package / 'SHA256SUMS').read_text().splitlines()
        assert sums == [f'{hashlib.sha256((package / name).read_bytes()).hexdigest()}  {name}'
                        for name in entry.values()]
        apply_build_info({'variant': 'full', 'off': ''}, 'full \n')
    with tempfile.TemporaryFile() as stream, zipfile.ZipFile(stream, 'w') as archive:
        for name in entry.values():
            archive.writestr(name, '')
        check_archive_images(archive, [entry])
        try:
            check_archive_images(archive, [dict(entry, image='rfoundry_test.bin')])
        except ValueError:
            pass
        else:
            raise AssertionError('ZIP validation accepted a source filename instead of the packaged member')
    default_readme = package_readme('test-version', parrot=False, without_aes=False)
    diagnostic_readme = package_readme('test-version', parrot=True, without_aes=True)
    assert "DMR AES follows each board's default" in default_readme and 'diagnostic is off' in default_readme
    assert 'DMR AES is disabled' in diagnostic_readme and 'disables DMR IP gateway' in diagnostic_readme
    assert all('Settings > Advanced > AES recovery' in readme for readme in (default_readme, diagnostic_readme))
    assert 'YSF TEST' in package_readme('test-version', False, False, ysf=True) and 'YSF' not in default_readme
    product = {'name': 'Crucible', 'issues': 'https://example.invalid/issues', 'flasher': 'https://example.invalid/f/'}
    crucible_readme = package_readme('test-version', False, False, ysf=True, product=product)
    assert 'Crucible pre-release' in crucible_readme and 'not RadioFoundry' in crucible_readme
    assert 'Crucible release,' in package_readme('v0.1.0', False, False, product=product, prerelease=False)
    assert 'https://example.invalid/f/' in crucible_readme and 'RadioFoundry web flasher' not in crucible_readme
    assert 'Private KF8S' not in crucible_readme and 'Private KF8S' in default_readme
    assert 'Settings > Advanced > P25 calibration' in diagnostic_readme and 'Limited blocks' in diagnostic_readme
    test_release_notices()
    print('kf8s build: branch guards, daily sequence, release labels, provenance, archive, notice and README checks passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--without-aes', action='store_true',
                        help='disable DMR AES on every image; otherwise keep board defaults')
    parser.add_argument('--parrot', action='store_true',
                        help='build the temporary P25 calibration profile with RAM-heavy optional services disabled')
    parser.add_argument('--board', action='append', choices=sorted(boards.BOARDS),
                        help='build only this registry board (repeat for multiple boards)')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    branch = git('branch', '--show-current')
    prefix = require_branch(branch)
    if git('status', '--porcelain', '--untracked-files=normal'):
        raise ValueError('commit or preserve pending changes before building')
    # Start from board defaults. Docker receives explicit make arguments and release.py needs the same overrides.
    build_env = {key: value for key, value in os.environ.items()
                 if not key.startswith('CONFIG_') and key not in ('VARIANT', 'MAKEFLAGS', 'MFLAGS', 'GNUMAKEFLAGS')}
    profile_flags = ['CONFIG_DMR_AES=n'] if args.without_aes else []
    if args.parrot:
        profile_flags += [f'CONFIG_{feature}=n' for feature in
                          ('DMR_IP_GATEWAY', 'USB_NETWORK', 'NET_PPP', 'APRS', 'SFR', 'HOTSPOT', 'SATELLITE_TLE')]
    # Crucible's releases carry only its radios (crucible.toml); the old labels build every board
    product = crucible.config(ROOT)['project'] if prefix == 'crucible' else None
    shipped = crucible.board_ids(ROOT) if product else sorted(boards.BOARDS)
    if args.board and set(args.board) - set(shipped):
        raise ValueError('not a board this release ships: ' + ', '.join(sorted(set(args.board) - set(shipped))))
    selected = [boards.BOARDS[b] for b in shipped if not args.board or b in args.board]
    source_repo = product['repo'].removeprefix('https://github.com/') if product else 'dbehnke/RadioFoundry'
    remote = upstream_remote()
    subprocess.run(['git', 'fetch', remote, 'main'], cwd=ROOT, check=True)
    subprocess.run(['git', 'merge-base', '--is-ancestor', f'{remote}/main', 'HEAD'], cwd=ROOT, check=True)
    upstream_full = git('rev-parse', f'{remote}/main')
    commit_full = git('rev-parse', 'HEAD')
    commit_stamp = git('rev-parse', '--short=8', 'HEAD')
    upstream, commit = upstream_full[:8], commit_full[:8]
    csky_board = next((board for board in selected if board.platform == 'hrc7000'), None)
    if csky_board is not None:
        check_csky_checkout(csky_board, commit_full, build_env)
    day = datetime.now(ZoneInfo('America/Detroit')).strftime('%y%m%d')
    common = Path(git('rev-parse', '--git-common-dir'))
    if not common.is_absolute():
        common = ROOT / common
    if product:
        # semantic versions: one build name per version, never one already tagged here or on origin
        version = crucible_label(crucible.release_version(ROOT), release_tags('origin'))
        prerelease = crucible.release_prerelease(ROOT)
    else:
        sequence = next_sequence(common / 'kf8s-build-sequence.json', day)
        version = f'{prefix}-{upstream}-{commit}-{day}-{sequence}'
    out = ROOT / 'build' / 'kf8s' / version
    if out.exists():	# a Crucible version reuses its name after a failed attempt
        raise ValueError(f'{out} exists from an earlier attempt: look at it, then remove it to build {version} again')
    package = out / 'package'
    package.mkdir(parents=True)
    print(version, flush=True)
    images = []
    for board in selected:
        choices = [[(option['name'], value['id']) for value in option['values']] for option in board.build_options]
        for values in itertools.product(*choices):
            options = dict(values)
            name = board.release_id(options)
            build = out / name
            option_flags = [f'{key}={value}' for key, value in values]
            baseline = configured_features(board, [*profile_flags, 'CONFIG_P25_PARROT=n', *option_flags], build_env)
            parrot = args.parrot and 'P25' in baseline
            image_flags = [*profile_flags, 'CONFIG_P25_PARROT=' + ('y' if parrot else 'n')]
            flags = [f'BOARD={board.id}', f'BUILD={build}', f'RFOUNDRY_RELEASE={version}',
                     'BUILD_SOURCE=fork', f'BUILD_SOURCE_REPO={source_repo}', *image_flags, *option_flags]
            print('building ' + name, flush=True)
            log = out / (name + '.log')
            run_logged(build_command(board, ['make', f'-j{args.jobs}', *flags, 'all', 'stack-check']), log, build_env,
                       attempts=10 if board.platform == 'hrc7000' else 1)
            image = build / Path(board.build_image(options)).name
            expected = configured_features(board, [*image_flags, *option_flags], build_env) if parrot else baseline
            check_built_profile((build / 'version').read_text(), (build / 'flags').read_text(),
                                commit_stamp, version, expected)
            release = [sys.executable, 'tools/release.py', '--board', board.id, '--version', version,
                       '--git', commit_full, '--bin', str(image), '--out', str(package)]
            if board.platform == 'hrc7000':
                output = subprocess.check_output(build_command(board, ['csky-elf-gcc', '--version']),
                                                 cwd=ROOT, env=build_env, text=True)
                compiler = next(line for line in output.splitlines() if line.startswith('csky-elf-gcc '))
                release += ['--toolchain', compiler]
            for key, value in values:
                release += ['--build-option', f'{key}={value}']
            release_env = build_env.copy()
            release_env.update(flag.split('=', 1) for flag in image_flags)
            subprocess.run(release, cwd=ROOT, env=release_env, check=True)
            manifest_path = package / f'rfoundry-{name}-{version}.json'
            manifest = json.loads(manifest_path.read_text())
            check_image_manifest(manifest, expected)
            apply_build_info(manifest, (build / 'build_info').read_text())
            manifest_path.write_text(release_manifest.dumps(manifest), encoding='utf-8')
            images.append({'board': board.id, 'options': options, 'image': manifest['image']['file'],
                           'manifest': manifest_path.name,
                           'build_overrides': image_flags})
            shutil.rmtree(build)
    if (git('branch', '--show-current') != branch or git('rev-parse', 'HEAD') != commit_full or git('status', '--porcelain') or
            git('rev-parse', f'{remote}/main') != upstream_full):
        raise ValueError('source changed during the build; refusing to package')
    write_release_sums(package, images)
    profile = 'p25-calibration' if args.parrot else 'agc'
    if args.without_aes:
        profile += '-test-no-aes'
    metadata = {'version': version, 'upstream': upstream_full,
                'commit': commit_full, 'profile': profile,
                **({'prerelease': prerelease} if product else {}),
                'build_overrides': profile_flags, 'images': images}
    (package / 'build.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (package / 'README.txt').write_text(package_readme(version, args.parrot, args.without_aes,
                                                       ysf=prefix in ('kf8sysf', 'crucible'), product=product,
                                                       prerelease=not product or prerelease))
    for name in ('LICENSE', 'LICENSE-MIT', 'CREDITS.md', 'AUTHORS', 'CONTRIBUTORS'):
        shutil.copy2(ROOT / name, package / name)
    shutil.copytree(ROOT / 'LICENSES', package / 'LICENSES')
    copy_firmware_notices(package, images, build_env)
    # crucible-v0.1.0.zip (the flasher site takes crucible-*.zip); the older labels get radiofoundry- in front
    archive = out / (f'crucible-{version}.zip' if product else f'radiofoundry-{version}.zip')
    digest = write_archive(package, archive, images)
    print(f'ZIP: {archive}\nSHA256: {digest}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
