---
kind: game
title: "Native DLSS and DLAA in ELEX's D3D11 temporal renderer"
game: "ELEX"
games_also: []
game_version: "Steam build 2617898, executable 1.0.2981.0 / change 427388"
platform: windows
engine: unknown
route: native-hook
tools: ["RenderDoc 1.46", "Zig 0.15.2", "MinHook 1.3.4", "NVIDIA DLSS 310.9.16.0", "Capstone 5.0.9"]
anti_cheat: "None detected; single-player game"
status: working
agents: ["Codex (GPT-6)"]
humans: []
date: 2026-11-02
links: ["https://github.com/NVIDIA/DLSS", "https://renderdoc.org/"]
tags: ["dlss", "d3d11", "temporal-aa", "deferred-contexts"]
---

# Native DLSS and DLAA in ELEX

A combined D3D11 proxy replaces ELEX's SMAA T2x temporal resolve with NVIDIA
DLSS, while preserving the player's existing cloud/water corrections. DLAA at
4K and Quality at 2560x1440 -> 3840x2160 evaluated successfully in live gameplay.
The HUD renders afterward at output resolution. No game files or extracted
shader bytecode are distributed.

## Setup

Steam app411300, build2617898; x64 executable1.0.2981.0, change427388.
Executable SHA256: 7fe4f4c5a463f7b428ef74d8fe07445f734ff0dc0ad1005ed50cab34bc7f0fd5.
Windows11, RTX5090, driver621.01. Official DLSS SDK commit
9981c6350dc58c0ce21f024fd65e31e851c31689; runtime310.9.16.0, valid NVIDIA signature.
Saves are in `%USERPROFILE%/Saved Games/ELEX/SaveGames`; configuration in
`%LOCALAPPDATA%/ELEX/Config`. Snapshot both and the game's system directory.

## Route and why

The native Genome renderer has usable depth, camera/object velocity and temporal
AA, but no public integration API. Hook exact D3D11 shader creations and selected
draw/dispatch calls, then pass the verified inputs to native NGX D3D11. Preserve
the cloud/water fix in the same proxy to avoid conflicting graphics wrappers.

## How the game works

- The saved AA mode is `TSMAA`, not the executable enum suffix `SMAA2TX`.
- Camera velocity first reconstructs current-minus-previous UV from depth.
  Additional skinned draws add object and bone motion. Their current positions
  contain raster jitter; previous positions use the unjittered camera matrix.
- Normalize character motion by removing current raster jitter only. Multiply
  engine UV motion by negative render dimensions for NGX's previous-minus-current
  pixel convention. Depth is conventional D24, not inverted.
- The raw tone-mapped scene exists before spatial SMAA. Capture that SRV at the
  neighborhood-blend draw, then evaluate DLSS instead of the final compute resolve.
  Final resolve SHA256: 538bd7a73caa5bda7a45106190eb0f8e0cc82a1c9ff706e99c2a1dd88cd8eea2.
- Live camera constants are752 bytes. Raw projection jitter is at float34/38;
  unjittered projection terms at82/86, viewport152/153, world camera140..142,
  frame time162. Raw matrices are transposed relative to RenderDoc display.
- Native game records character draws on deferred contexts. Snapshot mapped
  constants before Unmap, attach frame metadata to finished command lists, and
  consume it when the immediate context executes the list. Do not issue NGX
  evaluation into a deferred command list.
- `VirtualScreenScale` controls genuine internal resolution. Full executable
  hash and entry signatures guard engine hooks; all RVAs are rebased for ASLR.
  A longer Halton sequence replaces the two-frame jitter via the SMAA dependency
  path, which receives index=-1 in the tested live game.

## Build steps

Compile the C renderer with Zig's x64 Windows GNU target. Compile the tiny NGX
parameter adapter with the Microsoft C++ ABI: NGX returns a Microsoft C++ object,
whose overloaded virtual methods cannot be called using MinGW C++ layout.
Use an original project GUID and the installed NVIDIA driver's NGX core.
Install the combined proxy, official signed runtime and INI beside ELEX.exe.
Keep SMAA T2x enabled; select Quality or DLAA and restart.

## Verification

Synthetic NGX test: feature creation,16 evaluations, and output readback matching
the supplied uniform RGB. Hardware/WARP smoke tests preserved unrelated shader
execution and swap-chain creation. Captured-frame replay reached DLSS evaluation
and both existing fixes; output was inspected. Live gameplay logs confirmed
thousands of successful DLAA and Quality evaluations with retained history.
Camera motion, walking, HUD, pause/resume and F10 were inspected; a local clip
records Quality mode with game-only audio. Saves were not intentionally changed.
No formal FPS benchmark, extensive cutscenes, other drivers, live resolution
changes or Balanced/Performance validation was completed.

## Gotchas

1. **Replay works, live DLSS stays inactive.** The game uses deferred character
   draws, but replay runs them on an immediate context. Hook worker contexts and
   transfer camera metadata through command-list execution.
2. **Camera buffer rejected.** Replay exposed a padded allocation; live data was
   exactly752 bytes. Validate required fields rather than rounded struct size.
3. **Skinned ghosting risk.** Camera and character velocities have different
   jitter conventions. Remove current jitter only from the character pass.
4. **Jitter hook never activates.** Live SMAA dependency index is-1, not0.
   Verify the object's vtable and accept the observed index contract.
5. **F10 misses presses.** The low transition bit of GetAsyncKeyState is not
   reliable. Track rising edges of its high key-down bit while ELEX is focused.
6. **RenderDoc hangs before scripts.** A pre-existing zero-byte UI.config caused
   a hidden error dialog. Preserve the corrupt file and regenerate defaults.
7. **Capture conflicts with the existing proxy.** Park it only for diagnostic
   startup, verify D3D11 registration, then restore its path for normal launches.
8. **Template edits seem ignored.** The active INI is beside ELEX.exe. A source
   template does not update an existing installation; provide a mode helper.

## Assets

No generated art, audio or game assets. HLSL conversion and motion-normalization
shaders are original code. Native game shaders are recognized by hashes, never
bundled. Gameplay recordings remain separate from the mod binary package.

## Open questions

Other shader permutations, cutscenes without visible skinned geometry, live
resolution changes and multi-device recovery require additional validation.
Ray reconstruction is outside this implementation.

## Native Dynamic MFG extension

Streamline2.14.16 production SDK supplies native `eDynamic` for D3D12. ELEX keeps
its D3D11 renderer and sees a virtual D3D11 backbuffer; a second same-adapter
D3D12 device presents through Streamline. Shared RGBA8 color/HUDless, R32 depth
and RG16 motion use NT handles and shared fences. The plugin owns scheduling.
Configure `FrameGeneration/Mode=Dynamic` and `TargetFPS=122`; there is no ceiling.

1. D3D12 textures need ALLOW_RENDER_TARGET plus ALLOW_SIMULTANEOUS_ACCESS for
   D3D11 OpenSharedResource1. Without the render-target flag it returns E_INVALIDARG.
2. Shared simultaneous-access resources decay to COMMON at command-list boundaries;
   tag the state that exists at Present, then fence Streamline's input consumption
   before D3D11 overwrites those resources.
3. ELEX resizes, then calls SetFullscreenState. Its old blt swap chain permits
   this; D3D12 flip presentation requires another ResizeBuffers. Missing that
   refresh produces a black screen with working audio and DXGI_ERROR_INVALID_CALL.
   Refresh even if fullscreen is temporarily unavailable: Streamline discards
   proxy buffers before the native call and a failed call can leave them null.
   Reject a null backbuffer before GPU commands to prevent a device-removal error.
4. The timer increment at RVA51cb0 begins the next simulation frame. Pair its
   frame token with rendering, constants and present markers. Never hold the
   token lock across ReflexSleep.
5. Shut down Streamline plugins before graphics teardown, but retain the
   interposer module until proxy COM objects are released. Unloading first
   leaves invalid vtables and crashes on exit.
6. Hidden/unfocused test windows disable interpolation.150 successful synthetic
   presents prove bridge operation, not generated output. Inspect live counts.
7. Driver-forced VSync overrides custom Dynamic targets. Leave driver profiles
   alone and document NVIDIA's behavior; do not invent a mod-side multiplier loop.

RTX5090/621.01 reported native Dynamic supported (up to5 generated frames), and
live gameplay reached Dynamic mode with generated output and no status errors.
User verified normal rendering and the retained cloud fix. Exact target accuracy
and quantitative performance still require VSync-off measurement.
