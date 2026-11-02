# ELEX native DLSS 0.1.0

Unofficial native D3D11 integration of NVIDIA DLSS Super Resolution and DLAA for
ELEX, with the existing cloud/water flicker corrections included.

Native **DLAA and DLSS Quality have been verified in live gameplay** on the
supported build below. Quality renders at **2560x1440 and reconstructs 3840x2160**;
the HUD stays at output resolution. Frame generation and ray reconstruction are
not implemented. This is an early release tested in one saved outdoor scene.

## What is implemented

- Exact SMAA T2x shader recognition, raw scene color before SMAA and the HUD.
- Engine D24 depth, camera and skinned-character motion, with character jitter
  removed from motion and motion converted to DLSS's direction and pixel scale.
- Native NGX D3D11 evaluation and full context-state restoration.
- Guarded render-scale and Halton-jitter engine hooks for the exact supported
  executable; no game executable is changed on disk.
- F10 toggles DLSS at the temporal resolve. Unknown shaders retain engine rendering.
- Immediate and deferred rendering paths; camera constants follow recorded
  command lists, without synchronous per-frame GPU readbacks.
- Cloud execution-barrier fix and separate water-target draw workaround.

## Supported configuration

ELEX Steam app 411300, build 2617898, executable 1.0.2981.0 / change 427388,
SHA256 `7fe4f4c5a463f7b428ef74d8fe07445f734ff0dc0ad1005ed50cab34bc7f0fd5`.
Test hardware: RTX 5090, driver 621.01, Windows 11, D3D11, 3840x2160.
NGX runtime 310.9.16.0 from the official NVIDIA SDK.

The installer currently expects the existing cloud/water fix v0.2.0 DLL.
It refuses to replace an unknown wrapper or a mismatched game build.

## Use the installed mod

Start ELEX normally through Steam and keep anti-aliasing on **SMAA T2x**.
The mod replaces that temporal pass. **Quality is the installed default.**
The active configuration is **`ELEX/system/elex-dlss.ini` beside ELEX.exe**.
The INI in this source folder is a distribution template; editing it alone does
not change the already installed game.

With ELEX closed, either edit the active INI or run:

```powershell
.\tools\set-mode.ps1 -Mode Quality
```

Use `DLAA` for native-resolution reconstruction. Restart after changing modes.
F10 switches the temporal reconstruction off/on for comparison while keeping the
selected internal render scale. It does not switch Quality mode back to native 4K.

| Mode | Internal scale | Verification |
| --- | --- | --- |
| DLAA | 100% | Live 3840x2160 -> 3840x2160 |
| Quality | 66.7% | Live 2560x1440 -> 3840x2160 |
| Balanced | 58% | Implemented; not gameplay-tested |
| Performance | 50% | Implemented; not gameplay-tested |

## Install from the binary package

Close ELEX, extract the ZIP to a persistent folder and run `tools/install.ps1`
to review the changes, then `tools/install.ps1 -Apply` to install. Windows may
request permission to write to the Steam game folder. Keep the extracted folder:
its backups and installation manifest are needed for rollback. Do not blindly
overwrite an unrelated graphics wrapper.

## Build and test

Use Zig 0.15.2 on Windows and Python 3. Obtain the official NVIDIA DLSS SDK
commit `9981c6350dc58c0ce21f024fd65e31e851c31689` in `.deps/DLSS`.
Read its license before distributing SDK components.

```powershell
.\build.ps1 -Zig 'C:\Tools\zig\zig.exe'
.\build\ngx-probe.exe "$PWD\.deps\DLSS\lib\Windows_x86_64\rel" "$PWD\build\ngx-data"
.\tools\install.ps1 -Mode Quality        # review-only dry run
.\tools\install.ps1 -Mode Quality -Apply # close ELEX first
```

The installed log is `system/elex-dlss.log`. Require **DLSS EVALUATED** from the
current process before reporting DLSS active. Startup/shader-match lines alone
do not demonstrate evaluation. Cloud and water each have separate APPLIED lines.

To roll back with ELEX closed, run `tools/rollback.ps1` to review, then add
`-Apply`. It checks installed hashes before restoring the prior graphics DLL.
Backups and an installation manifest remain in the workspace's ignored backups
directory. Subsequent independent game-configuration edits are preserved.

## Validation and limits

- Native NGX synthetic-input test: 16 evaluations with correct GPU readback.
- Captured ELEX gameplay frame: compiled DLL evaluated DLSS, cloud and water
  corrections applied, output inspected.
- Native hardware and WARP smoke tests: unrelated shader execution and
  device/swap-chain forwarding passed.
- Live DLAA and Quality: thousands of successful evaluations with history kept
  across frames; camera movement, walking, HUD, pause/resume and F10 tested.
- No formal FPS benchmark was run. CPU limits and DLSS overhead can limit gains.

Tested with one D3D11 rendering device and the exact shader permutations and
constant-buffer layout recorded in the supplied scene. Other locations,
cutscenes, live resolution changes, multi-device recovery, other GPUs/drivers,
Balanced and Performance remain unverified. Restart the game after resolution
or mode changes. Engine hooks require the exact executable fingerprint and entry
signatures. Do not combine with a separate DXVK/ReShade D3D11 wrapper.
Windows Auto HDR was left as configured; recordings are visual evidence rather
than a color-accuracy benchmark.

## Credits and licenses

Original integration created with an AI coding assistant using the Universal
Modder workflow. The player supplied hardware, gameplay captures and testing.
MIT cloud/water code: [license](./third_party/cloud-water/LICENSE.txt), derived
from the player's Elex-Flicker-fix project revision
`cbfea33d4047cdea1916123c89776d7a48d4a055`.
MinHook 1.3.4: [license](./third_party/minhook/LICENSE.txt).
[NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS) is licensed separately.
[RenderDoc](https://renderdoc.org/) is used only for local analysis.
This software contains source code provided by NVIDIA Corporation. NVIDIA's SDK
headers/runtime retain their proprietary license; they are not covered by this
project's MIT license. NVIDIA has not endorsed this community mod.

No ELEX executables, shader bytecode, captures, extracted textures or saves are
part of the source distribution. All game-derived analysis stays in a private
local lab. Do not publish the ignored backups, SDK checkout or build caches.
