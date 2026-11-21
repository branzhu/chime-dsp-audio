#!/usr/bin/env python3
"""Prepare a local, history-free publication candidate. Never commits/pushes."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PUBLIC_DOCS = {
    'docs/README.md', 'docs/build.md', 'docs/architecture.md',
    'docs/safety.md', 'docs/licenses.md', 'docs/publishing.md',
}
LICENSE_FILES = {
    'LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md', 'LICENSES/README.md',
    'LICENSES/CC-BY-4.0.txt', 'LICENSES/CERN-OHL-P-2.0.txt',
    'LICENSES/LGPL-2.1.txt', 'LICENSES/LicenseRef-Espressif-Modified-MIT.txt',
    'LICENSES/TinyUSB-MIT.txt', 'LICENSES/cJSON-MIT.txt',
    'LICENSES/USB-descriptors-MIT.txt', 'LICENSES/LHDC-V5-NOTICE.md',
}
LICENSE_BLOCKER = 'Project license texts, scope and third-party notices are missing or empty.'
PUBLIC_FILES = PUBLIC_DOCS | LICENSE_FILES | {
    'README.md', '.gitattributes', '.gitignore', 'LICENSE.md',
    'hardware/README.md', 'hardware/audio-dsp.epro2', 'mechanical/README.md',
    'mechanical/front.stp', 'mechanical/back.stp',
    'mechanical/front.3mf', 'mechanical/back.3mf',
    'software/README.md', 'software/CMakeLists.txt', 'software/dependencies.lock',
    'software/partitions.csv', 'software/sdkconfig', 'software/sdkconfig.defaults',
}
PUBLIC_TREES = (
    'assets/branding/', 'dsp/', 'scripts/', 'software/components/',
    'software/main/', 'software/tests/', 'tools/wireshark/',
)
PRIVATE_PARTS = {
    '.git', '.codex', '.agents', '.vscode', '.idea', '__pycache__',
    'node_modules', '.cache',
}
ARTIFACT_SUFFIXES = {
    '.pdb', '.obj', '.o', '.exe', '.dll', '.pyc', '.pyo',
    '.tmp', '.bak', '.key', '.pem',
}
# These are deliberately local-review blockers, not an automated legal verdict.
CODEC_BLOCKER = (
    'Imported reference code is included: LDAC and native_bt adapter redistribution '
    'rights remain unresolved (the separate LHDC decoder has an Apache-2.0 '
    'declaration). An attribution or use-at-your-own-assessment disclaimer does '
    'not supply permission. Obtain sufficient authorization or remove the actual '
    'sources/dependencies and validate a buildable replacement; '
    'a disabled Kconfig option is not source removal.'
)


def is_public(name: str) -> bool:
    p = PurePosixPath(name)
    if p.is_absolute() or '..' in p.parts or '\\' in name or ':' in name:
        return False
    if any(part.lower() in PRIVATE_PARTS or part.lower().startswith('.env') for part in p.parts):
        return False
    if p.suffix.lower() in ARTIFACT_SUFFIXES:
        return False
    return name in PUBLIC_FILES or name.startswith(PUBLIC_TREES)


def inventory(root: Path) -> list[str]:
    # A new candidate has no .git. Never query a private ancestor repository.
    if not (root / '.git').exists():
        paths = [name for name in PUBLIC_FILES if (root / name).is_file()]
        for prefix in PUBLIC_TREES:
            base = root / prefix
            if base.is_dir():
                paths.extend(p.relative_to(root).as_posix() for p in base.rglob('*') if p.is_file())
        return sorted(set(paths))
    result = subprocess.run(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
        cwd=root, check=True, capture_output=True,
    )
    return sorted(set(result.stdout.decode('utf-8').rstrip('\0').split('\0')) - {''})


def load_snapshot(root: Path, candidates: list[str]) -> dict[str, bytes]:
    blobs = {}
    for name in sorted(set(candidates)):
        if not is_public(name):
            continue
        source = root / name
        # Reject symlinks at any depth, even if their target happens to be local.
        current = root
        for part in PurePosixPath(name).parts:
            current /= part
            if current.is_symlink():
                raise ValueError(f'Symlink is not publishable: {name}')
        if not source.resolve().is_relative_to(root.resolve()):
            raise ValueError(f'Public input escapes workspace: {name}')
        if not source.is_file():
            raise ValueError(f'Missing public input: {name}')
        blobs[name] = source.read_bytes()
    required = PUBLIC_FILES - {'LICENSE.md'}
    missing = sorted(required - blobs.keys())
    if missing:
        raise ValueError('Missing public files: ' + ', '.join(missing))
    validate_pins(blobs)
    return blobs


def validate_pins(blobs: dict[str, bytes]) -> None:
    def require_hash(name: str, digest: str) -> None:
        if name not in blobs:
            raise ValueError(f'Pinned source omitted: {name}')
        if hashlib.sha256(blobs[name]).hexdigest() != digest:
            raise ValueError(f'Pinned source changed: {name}')

    for component, prefix in [('chime_codec_reference', 'vendor/'), ('chime_freeaptx', '')]:
        base = f'software/components/{component}/'
        pins = json.loads(blobs[base + 'UPSTREAM.json'])['files']
        for name, digest in pins.items():
            require_hash(base + prefix + name, digest)
    manifest = json.loads(blobs['software/main/generated/manifest.json'])
    for name, digest in manifest['source_sha256'].items():
        require_hash(name, digest)
    for name, data in blobs.items():
        if name.startswith('software/tests/bluetooth/fixtures/') and name.endswith('/PROVENANCE.json'):
            for relative, digest in json.loads(data).get('files', {}).items():
                if isinstance(digest, str):
                    require_hash(str(PurePosixPath(name).parent / relative), digest)


def blockers(blobs: dict[str, bytes]) -> list[str]:
    result = []
    if any(not blobs.get(name, b'').strip() for name in LICENSE_FILES):
        result.append(LICENSE_BLOCKER)
    if any(name.startswith('software/components/chime_codec_reference/vendor/') for name in blobs):
        result.append(CODEC_BLOCKER)
    return result


def write_snapshot(root: Path, destination: Path, blobs: dict[str, bytes]) -> dict:
    root = root.resolve()
    if any(not is_public(name) for name in blobs):
        raise ValueError('Snapshot contains a non-public path')
    # Only create a new folder under workspace/out. No overwrites/deletes.
    destination = destination if destination.is_absolute() else root / destination
    destination = destination.absolute()
    out = root / 'out'
    if out.is_symlink():
        raise ValueError('out/ must not be a symlink')
    resolved = destination.resolve()
    if not resolved.is_relative_to(out) or resolved == out:
        raise ValueError('Output must be a new directory below workspace/out/')
    current = root
    for part in destination.relative_to(root).parts:
        current /= part
        if current.is_symlink():
            raise ValueError('Output path must not contain symlinks')
    if destination.exists():
        raise ValueError('Output already exists; choose a new directory (no overwrite)')
    issues = blockers(blobs)
    status = {
        'schema': 1, 'status': 'blocked' if issues else 'manual-review-required',
        'history_included': False, 'publication_performed': False,
        'blockers': issues,
        'review_required': ['Secrets and personal metadata', 'PCB/3D/library rights',
                            'LDAC/native_bt authorization; disclaimers do not substitute for permission',
                            'Third-party notices, LGPL relinking and binary distribution obligations',
                            'Espressif-only codec terms and DSP-generated third-party content',
                            'Build/test from this snapshot', 'Explicit destination and visibility approval'],
        'files': {name: hashlib.sha256(data).hexdigest() for name, data in sorted(blobs.items())},
    }
    destination.mkdir(parents=True, exist_ok=False)
    for name, data in blobs.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (destination / 'PUBLICATION_STATUS.json').write_text(
        json.dumps(status, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n',
    )
    return status


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument('--check', action='store_true', help='Preview the publication whitelist')
    choice.add_argument('--output', type=Path, help='Create a new local candidate under out/')
    args = parser.parse_args()
    candidates = inventory(ROOT)
    blobs = load_snapshot(ROOT, candidates)
    print(f'Public candidate: {len(blobs)} files, {sum(map(len, blobs.values()))} bytes')
    if args.check:
        for name in sorted(blobs):
            print('INCLUDE ' + name)
        excluded = [name for name in candidates if name not in blobs]
        print(f'Excluded: {len(excluded)} private/history/artifact files')
        for name in excluded:
            print('EXCLUDE ' + name)
    else:
        status = write_snapshot(ROOT, args.output, blobs)
        print(f'Created local candidate: {args.output}; status={status["status"]}')
    for issue in blockers(blobs):
        print('PUBLICATION BLOCKER: ' + issue)
    print('No Git history, commits, remote changes or uploads were created.')
    return 0  # Preview/export succeeded; this is NOT a publication approval.


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(f'Public export refused: {error}', file=sys.stderr)
        sys.exit(1)
