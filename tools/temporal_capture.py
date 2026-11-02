"""Controlled RenderDoc launch. Run only after approval for temporary settings.

Requires ELEX_LAB. Saves an exact restoration manifest before modifying anything.
Preserves the installed proxy while avoiding the known RenderDoc hook conflict.
The user or approved automation loads a save, then F12 captures the temporal pass.
Call `python tools/restore_capture.py <lab>` after closing the game.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import time
import traceback

import renderdoc as rd

lab = Path(os.environ['ELEX_LAB']).resolve()
lab.mkdir(parents=True, exist_ok=True)
game = Path(r'C:\Program Files (x86)\Steam\steamapps\common\ELEX\system')
config = Path(os.environ['LOCALAPPDATA']) / 'ELEX' / 'Config' / 'ConfigUser.xml'
manifest_path = lab / 'capture-restore.json'
if manifest_path.exists():
    raise RuntimeError('A restoration manifest already exists. Restore that capture first.')

original = config.read_bytes()
if b'AntiAliasing="Off"' not in original:
    raise RuntimeError('Expected original AA=Off. Review the current settings before proceeding.')
changed = original.replace(b'AntiAliasing="Off"', b'AntiAliasing="SMAA2TX"', 1)
backup = lab / 'ConfigUser.before-temporal.xml'
backup.write_bytes(original)
proxy = game / 'd3d11.dll'
parked = game / 'd3d11.dll.dlss-capture-hold'
if parked.exists():
    raise RuntimeError('A parked proxy exists; refusing to overwrite it.')
proxy_hash = hashlib.sha256(proxy.read_bytes()).hexdigest()
if proxy_hash != '33b9b0a50c5bf34f1a8566acbd8f38210b2a64176627270c7a088ca749c48eb6':
    raise RuntimeError('Installed cloud/water DLL changed; review before parking it.')
manifest = {'config': str(config), 'backup': str(backup), 'original_sha256': hashlib.sha256(original).hexdigest(),
            'temporary_sha256': hashlib.sha256(changed).hexdigest(),
            'proxy': str(proxy), 'parked_proxy': str(parked), 'proxy_sha256': proxy_hash}
manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')

with (lab / 'temporal-launch.log').open('w', encoding='utf-8') as log:
    control = None
    try:
        config.write_bytes(changed)
        proxy.rename(parked)
        opts = rd.CaptureOptions()
        opts.hookIntoChildren = True
        env = []
        for name in ['SteamAppId', 'SteamGameId']:
            mod = rd.EnvironmentModification()
            mod.name = name; mod.value = '411300'; mod.mod = rd.EnvMod.Set
            env.append(mod)
        result = rd.ExecuteAndInject(str(game / 'ELEX.exe'), str(game), '', env,
                                    str(lab / 'temporal'), opts, False)
        log.write('Launch: %s target=%d\n' % (result.result, result.ident)); log.flush()
        manifest['target'] = result.ident
        manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        if result.result != rd.ResultCode.Succeeded:
            raise RuntimeError(str(result.result))
        control = rd.CreateTargetControl('', result.ident, 'ELEX DLSS temporal capture', True)
        if not control:
            raise RuntimeError('RenderDoc target control unavailable')
        deadline = time.time() + 40
        while time.time() < deadline:
            message = control.ReceiveMessage(None)
            if control.GetAPI():
                log.write('Capture API: %s\n' % control.GetAPI()); log.flush()
                control.TriggerCapture(1)
                break
            time.sleep(.2)
        else:
            raise RuntimeError('D3D11 capture API not registered; capture is not ready')
        deadline = time.time() + 10
        while time.time() < deadline:
            message = control.ReceiveMessage(None)
            if message.type == rd.TargetControlMessageType.NewCapture:
                log.write('Initial frame captured. Load a save and press F12 for gameplay.\n'); log.flush()
                break
            time.sleep(.1)
    except Exception:
        log.write(traceback.format_exc()); log.flush()
    finally:
        if control:
            control.Shutdown()
        # Subsequent ordinary launches must still have the user's graphics fix.
        if parked.exists() and not proxy.exists():
            parked.rename(proxy)
        log.write('Installed cloud/water proxy restored for subsequent launches.\n')
raise SystemExit
