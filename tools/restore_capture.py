"""Restore only the temporary temporal-AA change, preserving later user edits."""
import hashlib
import json
from pathlib import Path
import sys

lab = Path(sys.argv[1]).resolve(strict=True)
manifest_path = lab / 'capture-restore.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
config = Path(manifest['config'])
backup = Path(manifest['backup']).read_bytes()
if hashlib.sha256(backup).hexdigest() != manifest['original_sha256']:
    raise RuntimeError('Configuration backup checksum mismatch')
current = config.read_bytes()
if hashlib.sha256(current).hexdigest() == manifest['temporary_sha256']:
    config.write_bytes(backup)
elif b'AntiAliasing="SMAA2TX"' in current:
    config.write_bytes(current.replace(b'AntiAliasing="SMAA2TX"', b'AntiAliasing="Off"', 1))
elif b'AntiAliasing="Off"' not in current:
    raise RuntimeError('AA setting changed independently; refusing to overwrite it')
proxy, parked = Path(manifest['proxy']), Path(manifest['parked_proxy'])
if parked.exists():
    if proxy.exists():
        raise RuntimeError('Both proxy paths exist; refusing to overwrite either')
    if hashlib.sha256(parked.read_bytes()).hexdigest() != manifest['proxy_sha256']:
        raise RuntimeError('Proxy checksum mismatch')
    parked.rename(proxy)
if hashlib.sha256(proxy.read_bytes()).hexdigest() != manifest['proxy_sha256']:
    raise RuntimeError('Installed proxy checksum mismatch')
manifest['restored'] = True
manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('Temporary AA setting restored; original graphics DLL verified.')
