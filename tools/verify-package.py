"""Verify release archives without a GPU, SDK installation, or game files."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import zipfile

RUNTIME_SHA256 = 'd00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e'
STREAMLINE = json.loads(Path(__file__).with_name('streamline-runtime.json').read_text(encoding='utf-8'))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--version', required=True)
parser.add_argument('--dist', type=Path, default=Path(__file__).resolve().parents[1] / 'dist')
args = parser.parse_args()
if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?', args.version):
    raise SystemExit('Invalid package version')


def require(value, message):
    if not value:
        raise SystemExit(message)


def read_zip(path):
    with zipfile.ZipFile(path) as archive:
        require(archive.testzip() is None, f'Corrupt ZIP: {path.name}')
        names = archive.namelist()
        require(len(names) == len(set(names)), f'Duplicate ZIP entries: {path.name}')
        for name in names:
            require(not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts
                    and ':' not in name and '\\' not in name, f'Unsafe archive path: {name}')
        return {name: archive.read(name) for name in names if not name.endswith('/')}


binary_name = f'ELEX-DLSS-{args.version}-Windows-x64.zip'
source_name = f'ELEX-DLSS-{args.version}-Source.zip'
binary = read_zip(args.dist / binary_name)
source = read_zip(args.dist / source_name)
expected = {
    'system/d3d11.dll', 'system/nvngx_dlss.dll', 'system/elex-dlss.ini',
    'README.md', 'CHANGELOG.md', 'LICENSE', 'NVIDIA-LICENSE.txt', 'VERSION.txt',
    'third_party/cloud-water/LICENSE.txt', 'third_party/minhook/LICENSE.txt',
    'tools/install.ps1', 'tools/rollback.ps1', 'tools/set-mode.ps1',
    'tools/set-frame-generation.ps1', 'tools/settings.ps1', 'tools/streamline-runtime.json',
    'tools/nvidia-presets.ps1', 'tools/nvidia-profile.cs', 'third_party/nvapi/LICENSE.txt',
}
expected.update('system/' + name for name in STREAMLINE['files'])
expected.update('third_party/streamline/' + PurePosixPath(name).name for name in STREAMLINE['licenses'])
require(set(binary) == expected, f'Unexpected binary ZIP contents: {set(binary) ^ expected}')
require(binary['VERSION.txt'].decode('ascii').strip() == args.version, 'Binary version mismatch')
require(source['VERSION.txt'].decode('ascii').strip() == args.version, 'Source version mismatch')
require(hashlib.sha256(binary['system/nvngx_dlss.dll']).hexdigest() == RUNTIME_SHA256,
        'Unverified NVIDIA runtime')
require(b'Mode=Quality' in binary['system/elex-dlss.ini'], 'Missing default Quality configuration')
require(b'Preset=Default' in binary['system/elex-dlss.ini'], 'Missing default SR preset configuration')
require(b'Indicator=On' in binary['system/elex-dlss.ini'], 'Missing native indicator default')
require(b'[FrameGeneration]' in binary['system/elex-dlss.ini'] and b'Mode=Dynamic' in binary['system/elex-dlss.ini']
        and b'TargetFPS=122' in binary['system/elex-dlss.ini'], 'Missing native Dynamic MFG configuration')
require(json.loads(binary['tools/streamline-runtime.json']) == STREAMLINE, 'Runtime manifest mismatch')
for name, digest in STREAMLINE['files'].items():
    require(hashlib.sha256(binary['system/' + name]).hexdigest() == digest, f'Unverified Streamline runtime: {name}')

for name in ('system/d3d11.dll', 'system/nvngx_dlss.dll', *('system/' + n for n in STREAMLINE['files'])):
    data = binary[name]
    require(data[:2] == b'MZ' and len(data) >= 64, f'Not a Windows binary: {name}')
    pe = struct.unpack_from('<I', data, 60)[0]
    require(data[pe:pe+4] == b'PE\0\0' and struct.unpack_from('<H', data, pe+4)[0] == 0x8664,
            f'Not an x64 PE: {name}')
require(args.version.encode('ascii') in binary['system/d3d11.dll'], 'Version absent from compiled DLL')
require(b'elex_replay_attach' not in binary['system/d3d11.dll'], 'Replay-test DLL must not ship')

for name in source:
    parts = PurePosixPath(name).parts
    require(not {'.deps', '.tools', 'build', 'backups', 'private', '.git', '__pycache__'}.intersection(parts),
            f'Private/generated content in source ZIP: {name}')
    require(PurePosixPath(name).suffix.lower() not in {'.exe', '.dll', '.rdc', '.dxbc', '.cso', '.save', '.log', '.dmp'},
            f'Binary/game data in source ZIP: {name}')
for name in ('build.ps1', 'src/renderer.c', 'tools/resolve-version.ps1', 'tools/package.ps1',
             'tools/verify-package.py', 'tests/release-tools.ps1', '.github/workflows/release.yml'):
    require(name in source, f'Source package missing {name}')

checksums = {}
for line in (args.dist / 'SHA256SUMS.txt').read_text(encoding='ascii').splitlines():
    match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
    require(match is not None, f'Invalid checksum line: {line}')
    digest, name = match.groups()
    require(name not in checksums, f'Duplicate checksum: {name}')
    checksums[name] = digest
require(set(checksums) == {binary_name, source_name, 'system/d3d11.dll', 'system/nvngx_dlss.dll',
                          *('system/' + n for n in STREAMLINE['files'])},
        'Unexpected checksum entries')
for name, expected_hash in checksums.items():
    data = binary[name] if name.startswith('system/') else (args.dist / name).read_bytes()
    require(hashlib.sha256(data).hexdigest() == expected_hash, f'Checksum mismatch: {name}')
print(f'PASS: {args.version}; x64 DLL/runtime, licenses, end-user tools, source archive and checksums verified')
