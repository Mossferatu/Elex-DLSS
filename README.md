# ELEX native DLSS

Unofficial NVIDIA DLSS Super Resolution, DLAA and native Dynamic Multi Frame
Generation for ELEX, with the existing cloud/water flicker corrections included.
Super Resolution uses D3D11; frame generation uses a shared-resource D3D12
presentation bridge and NVIDIA Streamline's native scheduler.

Native **DLAA and DLSS Quality have been verified in live gameplay** on the
supported build below. Quality renders at **2560x1440 and reconstructs 3840x2160**;
the HUD stays at output resolution. Dynamic MFG has been tested in live gameplay
on RTX5090; this remains an early release with limited scene coverage.
Ray reconstruction is not implemented.

## What is implemented

- Exact SMAA T2x shader recognition, raw scene color before SMAA and the HUD.
- Engine D24 depth, camera and skinned-character motion, with character jitter
  removed from motion and motion converted to DLSS's direction and pixel scale.
- Native NGX D3D11 evaluation and explicit pipeline-state restoration, including
  ranged constant buffers and UAV counters, without changing D3D10/11 emulation.
- Guarded render-scale and Halton-jitter engine hooks for the exact supported
  executable; no game executable is changed on disk.
- F10 toggles DLSS at the temporal resolve. Unknown shaders retain engine rendering.
- Immediate and deferred rendering paths; camera constants follow recorded
  command lists, without synchronous per-frame GPU readbacks.
- Cloud execution-barrier fix and separate water-target draw workaround.
- Native Streamline `eDynamic` frame generation with an output FPS target,
  engine-frame Reflex markers, HUDless color, depth and motion. NVIDIA chooses
  the multiplier; no custom scheduler or multiplier ceiling is used.

## Supported configuration

ELEX Steam app 411300, build 2617898, executable 1.0.2981.0 / change 427388,
SHA256 `7fe4f4c5a463f7b428ef74d8fe07445f734ff0dc0ad1005ed50cab34bc7f0fd5`.
Test hardware: RTX 5090, driver 621.01, Windows 11, D3D11, 3840x2160.
Packaged SR and FG runtimes:310.9.16.0; Streamline:2.14.16 production binaries.
The local FG gameplay checks retained the player's SR runtime310.9.1.0.

Dynamic MFG requires compatible RTX50-series hardware, Windows Hardware-accelerated
GPU Scheduling, and driver595.41 or newer. The mod queries native support before
enabling it; unsupported systems retain the D3D11 Super Resolution path.
See [NVIDIA's Dynamic MFG guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.16/docs/ProgrammingGuideDLSS_G.md#63-enabling-dynamic-multi-frame-generation).

The installer supports a clean game installation or the known cloud/water fix
v0.2.0 DLL. Both corrections are included, so the older fix is not required.
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
Model presets are separate from these resolution modes; see below.
F10 switches the temporal reconstruction off/on for comparison while keeping the
selected internal render scale. It does not switch Quality mode back to native 4K.
Frame generation also pauses while F10 disables DLSS, because its temporal
inputs are captured at that pass.

### Native on-screen DLSS indicator

`[DLSS] Indicator=On` is the default. NVIDIA's own HUD displays the runtime's
selected preset, DLSS runtime version and rendering/output resolutions. Unlike
the configured preset hint, this identifies the model the runtime actually uses,
including NVIDIA App/driver overrides. It appears while the DLSS pass evaluates.

Set `Indicator=Off` to hide it for ELEX, or `Indicator=Driver` to leave NVIDIA's
existing indicator setting alone. Restart after changing this option:

```powershell
.\tools\set-mode.ps1 -Indicator On
.\tools\set-mode.ps1 -Indicator Off
```

This affects only the running mod: it supplies the indicator's registry-read
value inside the ELEX process. It does not edit the Windows registry or change
the indicator in other games. No RTSS installation is required for this HUD.

### RTSS compatibility

The SR-only path was tested with RTSS7.3.5.28211 on the test system at High
detection, with Stealth mode and Custom Direct3D support both off and both on.
Those two options were not required for the visible overlay on this system.
The previous missing-OSD issue was reproduced; the corrected build shows RTSS FPS
and the native NVIDIA indicator together while DLSS evaluates.

With Dynamic MFG enabled, gameplay and the native NVIDIA indicator also worked,
and RTSS recorded D3D12 statistics, but RTSS's on-screen FPS text was not visible.
The verified RTSS OSD fix therefore applies to `FrameGeneration/Mode=Off`.

The mod now preserves pipeline bindings explicitly instead of switching device
context emulation, which made RTSS treat the D3D11 game as D3D10. Hook forwarding
also retains its original function when overlays wrap a context's vtable, and
feature-level-10 probe devices are excluded from renderer initialization.
Recursive loader/device probes are covered by a bounded regression test.

The reporter's exact startup hang was not reproduced locally. Their RTSS version
and GPU were not supplied, so this is not a compatibility guarantee for every
RTSS/driver combination. The mod does not modify RTSS profiles. When reporting a
remaining problem, include RTSS version, GPU/driver, FG mode, and the current
`elex-dlss.log` startup section.

### Native Dynamic MFG

The active INI defaults to:

```ini
[FrameGeneration]
Mode=Dynamic
TargetFPS=122
```

Use `Mode=Off` for the original D3D11 presentation path. `TargetFPS=0` follows
the display refresh rate; a positive value selects the desired output FPS.
Restart ELEX after changing these settings, or use the package helper with ELEX closed:

```powershell
.\tools\set-frame-generation.ps1 -Mode Dynamic -TargetFPS 122
.\tools\set-frame-generation.ps1 -Mode Off
```

**A custom target requires VSync off, including any NVIDIA Control Panel override.**
NVIDIA ignores the custom target when VSync is enabled and targets a refresh-related
rate instead. The mod requests unsynchronized presentation for a positive target,
but does not change driver profiles. Native Dynamic MFG may select zero generated
frames when the rendered frame rate is already sufficient. The target is a request,
not a guarantee in every scene. See [NVIDIA's target and limiter behavior](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.16/docs/ProgrammingGuideDLSS_G.md#63-enabling-dynamic-multi-frame-generation).

In `elex-dlss.log`, `Native Dynamic MFG enabled` confirms the selected mode;
`FG PRESENT` reports the native status and generated-frame count. A zero count
does not demonstrate interpolation. The periodic FPS line is diagnostic timing,
not a frame-pacing benchmark. Menus, loading and missing temporal inputs disable
generation for that frame.

| Mode | Internal scale | Verification |
| --- | --- | --- |
| DLAA | 100% | Live 3840x2160 -> 3840x2160 |
| Quality | 66.7% | Live 2560x1440 -> 3840x2160 |
| Balanced | 58% | Implemented; not gameplay-tested |
| Performance | 50% | Implemented; not gameplay-tested |

### Model presets

For Super Resolution/DLAA, set `Preset` in the active INI's `[DLSS]` section:

```ini
[DLSS]
Mode=Quality
Preset=K
```

Supported values are `Default`, `E`, `F`, `J`, `K`, `L`, and `M`, independent of
the resolution mode. `Default` lets NGX choose; E/F are deprecated legacy choices.
The pinned SDK removes A-D and marks G-I/N-O unusable, so those values are rejected
by the helper. The runtime logs a warning and falls back to Default for an invalid
manually edited value. Presets are NGX hints: runtime support and NVIDIA driver/App
overrides can affect the model actually selected. Restart after changing a preset.
See [NVIDIA's SR preset definitions](https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/include/nvsdk_ngx_defs.h).

With ELEX closed, the helper can change just the preset while retaining quality,
or change both:

```powershell
.\tools\set-mode.ps1 -Preset K
.\tools\set-mode.ps1 -Mode Quality -Preset M
```

**Frame Generation model presets use NVIDIA's ELEX driver profile.** Streamline2.14.16
has no public model-preset member. These commands use NVIDIA's public DRS API to
select `A`, `B`, `C`, or `Latest` for ELEX only:

```powershell
.\tools\set-frame-generation.ps1 -ShowPreset
.\tools\set-frame-generation.ps1 -Preset B
.\tools\set-frame-generation.ps1 -Mode Dynamic -TargetFPS 122 -Preset C
.\tools\set-frame-generation.ps1 -Preset Default
```

`Latest` lets the driver select its latest supported preset. `Default` restores
the two FG preset settings saved before the helper's first override; without a
saved override it leaves the existing NVIDIA settings alone. The helper saves
the original values in `backups/nvidia-fg-preset.json`, verifies readback, protects
later independent edits, and refuses profiles shared with another application.
Rollback restores an override created by this helper. VSync, frame limits, native
Dynamic scheduling and global settings are not changed by preset selection.

FG preset letters are **not SR preset letters**. B selects NVIDIA's enhanced FG
model; C is NVIDIA's newer performance mode. Availability depends on the installed
driver/runtime. See [NVIDIA's FG B description](https://www.nvidia.com/en-us/geforce/news/dlss-4-5-dynamic-multi-frame-generation-6x-mode-released/)
and [FG C description](https://www.nvidia.com/en-eu/geforce/news/dlss-5-faster-ai-model-hogwarts-legacy-rtx-spark-laptops/).
There is no `[FrameGeneration] Preset` INI key: use the helper or NVIDIA App for
that driver setting. Changes take effect after restarting ELEX. The helper needs
64-bit PowerShell and the NVIDIA driver; it reports a driver error if the setting
cannot be applied. SR `Preset=Default` and existing FG driver choices are retained
by default during installation.

## Install from the binary package

Download **`ELEX-DLSS-<version>-Windows-x64.zip`** from the release's Assets section.
GitHub's automatic "Source code" downloads do not contain the built DLL or DLSS
runtime. The Windows ZIP includes the mod and all required NVIDIA/Streamline DLLs,
the INI, installation/rollback tools
and required license notices; end users do not need Zig, Python or the SDK.

Run ELEX once to create its user configuration, then close it. Extract the ZIP
to a persistent folder. From PowerShell in that folder, run:

```powershell
.\tools\install.ps1                           # review changes
.\tools\install.ps1 -Apply                    # install in the default Steam path
.\tools\install.ps1 -Game 'D:\SteamLibrary\steamapps\common\ELEX' -Apply
```

Choose the installation command matching your game location. The installer sets
SMAA T2x and preserves the previous configuration. Windows may request permission
to write to the Steam game folder. Keep the extracted folder: its backups and
installation manifest are needed for rollback. A clean-install rollback removes
the mod proxy; an installation over the supported graphics fix restores that DLL.

## Automated GitHub releases

The [Build release assets workflow](.github/workflows/release.yml) runs when a
release is **published**, including a prerelease. Saving a draft does not run it.

1. Commit and push the workflow and supporting scripts to GitHub.
2. Create a release targeting a commit that includes them, using a tag such as
   `v0.1.1`, `0.1.1` or `v0.2.0-beta.1`.
3. Publish the release and wait for **Build release assets** to finish in Actions.
4. These files appear in the release's Assets section:

   - `ELEX-DLSS-<version>-Windows-x64.zip` — the end-user package.
   - `ELEX-DLSS-<version>-Source.zip` — source and build scripts.
   - `SHA256SUMS.txt` — archive and packaged DLL checksums.

The workflow uses Windows Server 2022, checksum-pinned Zig 0.15.2, and the exact
NVIDIA SDK commit listed below plus checksum-pinned Streamline2.14.16. It verifies runtime hashes, package contents,
version, licenses and checksums before uploading. The release tag determines the
archive version, `VERSION.txt`, and the DLL's startup/NGX version strings.

Uploads use GitHub's built-in `GITHUB_TOKEN`; no personal token or additional
secret is needed. Only the upload job has `contents: write` permission. Re-running
a successful release job replaces the three matching assets. To test without
publishing, use **Actions → Build release assets → Run workflow**, select a ref
and enter a version. That produces a downloadable Actions artifact only.

GitHub-hosted runners do not provide the RTX hardware or ELEX installation used
for gameplay validation. CI compiles the DLL and tests packaging/installer
behavior, indicator isolation, wrapped graphics hooks, and WARP pipeline state
and startup reentry. It does not run the NVIDIA GPU probe or prove that a new
renderer change works in game. See [GitHub's release-event documentation](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#release).

## Build and test locally

Use Zig 0.15.2 on Windows and Python 3. Obtain the official NVIDIA DLSS SDK
commit `9981c6350dc58c0ce21f024fd65e31e851c31689` in `.deps/DLSS`.
Read its license before distributing SDK components.
Run the fetch helper for the pinned production Streamline SDK and its headers.

```powershell
.\tools\fetch-streamline.ps1
.\build.ps1 -Zig 'C:\Tools\zig\zig.exe'
.\build\ngx-probe.exe "$PWD\.deps\DLSS\lib\Windows_x86_64\rel" "$PWD\build\ngx-data"
.\tools\install.ps1 -Mode Quality        # review-only dry run
.\tools\install.ps1 -Mode Quality -Apply # close ELEX first
```

To reproduce versioned release assets locally:

```powershell
.\tests\release-tools.ps1
.\tests\nvidia-presets.ps1
.\tools\test-indicator.ps1 -Zig 'C:\Tools\zig\zig.exe'
.\tools\test-graphics-hooks.ps1 -Zig 'C:\Tools\zig\zig.exe'
.\tools\test-pipeline-state.ps1 -Zig 'C:\Tools\zig\zig.exe'
.\build.ps1 -Zig 'C:\Tools\zig\zig.exe' -Version 0.2.0-beta.3
.\tools\test-proxy-reentry.ps1 -Zig 'C:\Tools\zig\zig.exe'
.\tools\package.ps1 -Version 0.2.0-beta.3
python .\tools\verify-package.py --version 0.2.0-beta.3
```

The packager rejects a version that differs from the last successful DLL build.

On compatible hardware, build the native FG diagnostics with
`tools/build-fg-tests.ps1 -Zig 'C:\Tools\zig\zig.exe'`. The executables
`build/fg-capability.exe` and `build/fg-bridge-smoke.exe` each take the absolute
Streamline runtime directory and log directory as arguments. The bridge test
uses original synthetic geometry; a hidden/unfocused window does not prove
interpolated output because NVIDIA disables it in that state.

The installed log is `system/elex-dlss.log`. Require **DLSS EVALUATED** from the
current process before reporting DLSS active. Startup/shader-match lines alone
do not demonstrate evaluation. Cloud and water each have separate APPLIED lines.

To roll back with ELEX closed, run `tools/rollback.ps1` to review, then add
`-Apply`. It checks installed hashes before restoring the prior graphics DLL.
Backups and an installation manifest remain in the workspace's ignored backups
directory. Subsequent independent game-configuration edits are preserved.

## Validation and limits

- Native NGX synthetic-input test: 16 evaluations with correct GPU readback.
- SR preset K and M hints:16 GPU evaluations each with correct readback; K also
  read from the installed INI and evaluated successfully in live gameplay.
- FG A/B/C/Latest accepted by native DRS in temporary sessions; sessions discarded
  and the persistent profile verified unchanged. Saved-setting/restore behavior
  is covered by isolated driver fixtures, including failed saves and later edits.
- Captured ELEX gameplay frame: compiled DLL evaluated DLSS, cloud and water
  corrections applied, output inspected.
- Native hardware and WARP smoke tests: unrelated shader execution and
  device/swap-chain forwarding passed.
- RTSS7.3.5 SR-only overlay and NVIDIA's native preset/resolution HUD inspected
  together. Normal hooks and Stealth/Custom Direct3D both tested; see the RTSS
  section for the FG overlay limitation and unreplicated startup-hang report.
- Live DLAA and Quality: thousands of successful evaluations with history kept
  across frames; camera movement, walking, HUD, pause/resume and F10 tested.
- Native Dynamic MFG: hardware capability confirmed,150-frame synthetic bridge
  test with clean shutdown, fullscreen transition corrected and live gameplay
  inspected. User confirmed normal gameplay and the retained cloud correction.
  Driver-forced VSync was present during initial testing, so those runs do not
  validate an exact122FPS custom target or establish a performance gain.
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
NVIDIA NVAPI DRS ABI definitions: [MIT notice](./third_party/nvapi/LICENSE.txt).
[NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS) is licensed separately.
[NVIDIA Streamline](https://github.com/NVIDIA-RTX/Streamline) and its NVIDIA
runtime components are licensed separately; their notices ship in
`third_party/streamline` in the binary package. Only production DLLs are packaged.
[RenderDoc](https://renderdoc.org/) is used only for local analysis.
This software contains source code provided by NVIDIA Corporation. NVIDIA's SDK
headers/runtime retain their proprietary license; they are not covered by this
project's MIT license. NVIDIA has not endorsed this community mod.

No ELEX executables, shader bytecode, captures, extracted textures or saves are
part of the source distribution. All game-derived analysis stays in a private
local lab. Do not publish the ignored backups, SDK checkout or build caches.
