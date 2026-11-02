# ELEX native DLSS integration journal

## Objective (2026-11-01)

Integrate NVIDIA DLSS Super Resolution into ELEX's native D3D11 renderer,
using engine scene color, depth, temporal motion and camera jitter, preserving
the installed cloud/water fix. A DLL loading successfully is not proof of DLSS.
Completion requires successful NGX evaluations and live visual validation,
including camera/object movement, HUD, transitions and resolution changes.

## Confirmed environment

- Empty starting workspace: `C:\Users\intel\Documents\Elex-DLSS`.
- Steam app 411300, build 2617898; executable 1.0.2981.0, change 427388, x64.
- Install: `C:\Program Files (x86)\Steam\steamapps\common\ELEX`.
- GPU: NVIDIA GeForce RTX 5090; Windows driver 32.0.16.2101 (621.01).
- Native Genome engine / D3D11. Universal Modder scan reports native x64,
  no anti-cheat signatures, no recognised loader.
- Current `system/d3d11.dll` is the user's cloud/water fix v0.2.0-test.
  Source in sibling `Elex-Flicker-fix`, MIT licensed, MinHook 1.3.4.
  Its journal records user-confirmed live success. Preserve that behavior.
- Config: `%LOCALAPPDATA%/ELEX/Config`.
  Saves: `%USERPROFILE%/Saved Games/ELEX/SaveGames` (confirmed 15 files).
- Existing local capture/tool lab:
  `C:\Users\intel\AppData\Local\Temp\vscode-agent-codex-sandbox-9VJCA5\elex-water-lab`.
  RenderDoc 1.46 and `water_frame2283.rdc` (3840x2160 gameplay) are available.
  Prior journal warns that capture injection conflicts with the cloud proxy;
  existing captures were made without that proxy. Replay is independent.

## Research and route

- Read requested mod-any-game skill plus game-recon, reverse-engineering,
  share-field-notes, native-engine playbook and game-automation.
- Searched local Universal Modder knowledge for ELEX/Genome: no relevant note.
- Full `python -m um` fails importing unrelated iwi module (numpy absent).
  Used direct `um.scan.scan`/`format_report` entry points successfully.
- Chosen route: inspect existing GPU captures first; then exact shader / engine
  hooks with NGX D3D11. No assumed motion-vector or jitter contract.
- No game files, configuration, saves, registry or driver settings changed.
- Do not publish game captures, shader bytecode/disassembly or extracted assets.

## Next

Identify anti-aliasing/postprocessing pass, depth and motion-vector provenance,
camera matrices and jitter. Establish an NGX capability probe and toolchain.
Only install a concrete reviewed prototype after the required confirmation.

## NGX backend verification

- Official NVIDIA DLSS SDK checkout: commit
  `9981c6350dc58c0ce21f024fd65e31e851c31689` in ignored `.deps/DLSS`.
- Existing Zig 0.15.2: prior sandbox QqmegG's zig directory.
- `build.ps1` / VS Code build task compile a standalone D3D11 test. Parameter
  adapters compile with the Microsoft C++ ABI (NGX virtual overload ordering
  must not use MinGW C++ ABI); all other original code compiles as C.
- Test succeeds: NGX Init, DLSS available=1, feature creation and 16 evaluations
  at 640x360 -> 960x540. Read-back RGB 0.24976, 0.49951, 0.74951 vs expected
  0.25, 0.5, 0.75. Logs in ignored build directory. NOT game integration proof.
- Driver-core loaded from HKLM NGXCore FullPath, `_nvngx.dll`; ProjectID uses
  an original project GUID, custom engine. No borrowed NVIDIA application ID.

## Capture analysis and tool recovery

- Original capture has AA disabled; only 290 shader resources, temporal-AA
  shaders are not loaded. Binary strings identify FXAA2TX/SMAA2TX,
  eCImageFilterTFXAAResolve/eCImageFilterTSMAAResolve and VelocityBuffer.
- EID 4414: PS ResourceId 24708 computes current UV minus previous UV using
  a depth reprojection matrix. It is camera-only velocity. AO consumes velocity
  at EID 6305. Per-object motion with temporal AA remains unverified.
- EID 7241: final scene postprocess writes swapchain; UI begins at EID 7265.
- RenderDoc startup hung on an empty pre-existing UI.config (0 bytes, timestamp
  2026-10-28). Environment-only profile isolation did not bypass Known Folders.
  Preserved exact empty file as UI.config.empty-before-elex-dlss-20261101 in
  Roaming/qrenderdoc; defaults regenerated. Analysis then opened capture.
- Closed only our exact stuck RenderDoc process IDs (4216, 17108, 9608).
- First metadata run finished indexing, then failed due to changed RenderDoc
  1.46 binding API names. Corrected helper to use Descriptor/UsedDescriptor.
- First lab: sandbox-IsZdaa/elex-dlss-lab. After credit reset new writable lab:
  sandbox-UkNMwk/elex-dlss-lab. Keep extracted material out of this repository.

## SMAA T2x capture confirmed (2026-11-01)

- User supplied new gameplay captures, confirmed SMAA T2x. Latest is
  `temporal_frame4430.rdc` in the UkNMwk lab; earlier frame4224 also retained.
- Actual config spelling is `AntiAliasing="TSMAA"`, not enum suffix SMAA2TX.
  User selected the mode. Game is now closed, cloud/water proxy restored.
- Backups: workspace `backups/um/backups/elex-dlss-saves/20261101-230549.zip`,
  config and system both `20261101-230550.zip` in their respective names.
- Raw tone-mapped scene: EID 11059 PS 21148 -> B8G8R8A8 resource 278485.
- SMAA neighbor: EID 11121 PS 24709 (SHA b49842b8...914e1), t0 velocity 24905,
  t1 raw scene 278485, t2 blend weights. Output R8G8B8A8 208739.
- Temporal resolve: EID 11128 CS 24712, 3484 bytes, SHA256
  538bd7a73caa5bda7a45106190eb0f8e0cc82a1c9ff706e99c2a1dd88cd8eea2.
  t0 velocity, t1 SMAA color, t2 history; u0 swapchain50; UI follows.
- Native depth: D24S8 typeless resource51; conventional non-inverted projection.
- Camera velocity EID8312 PS hash 6cf656ad...f9e8b: current UV - previous UV.
  Additional character draws EIDs8337..8374 PS hash cbcb7b0a...64e8a0 add
  object/bone motion. VS uses current jittered VP and previous unjittered VP.
- cb_Globals observed layout (bytes): VP0, invP64, P128, view192, cameraVP256,
  unjitteredP320, prevVP384, worldCamera560, viewport608, frameTime648.
  Native raw matrices are transposed relative to RenderDoc display.
  Raw projection jitter lives at floats34/38, unjittered at82/86.
  Current raster shift at3840x2160 is (-0.125,-0.125) pixels.
- Plan: normalize character velocities to remove current jitter only; retain
  native raster positions and depth. Feed raw pre-SMAA scene to DLSS at the
  exact temporal resolve, with pixel-unit motion scales (-width,-height).
  Capture globals at CPU Map/Unmap; avoid per-frame GPU readback.
- Imported MIT cloud/water source and MinHook1.3.4 into third_party. Parent
  fix source revision cbfea33d4047cdea1916123c89776d7a48d4a055.

## Live integration working (2026-11-02)

- User explicitly approved installation and agent control of ELEX for tests.
- Installed signed NVIDIA NGX runtime310.9.16.0. Original cloud/water DLL and
  user config retained in `backups/install-20261102-000211`; rollback state in
  `backups/installed.json`. Installed runtime SHA256
  d00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e.
- Compiled DLL currently installed SHA256
  12c925c66f4b12f789afc8bf15e0391ac1701d73030be32f6d7d1fdbf7ab37d3.
- Replay test succeeded on frame4430 with DLSS and both flicker corrections.
  Native hardware/WARP forwarding tests passed, unrelated CS still writes42.
- First live attempts safely retained native resolve because globals were
  absent. Actual game records object draws on deferred contexts; replay does
  not. Added Map/Unmap/DrawIndexed/FinishCommandList hooks for deferred contexts
  and command-list private metadata consumed by immediate ExecuteCommandList.
- Live globals are752 bytes; capture replay exposed65536-byte allocation.
  Fixed erroneous768-byte minimum; shadows permit ranged bindings.
- SMAA dependency calls pass index=-1 in live game. Accepted index<=0 for exact
  SMAA vtable. Halton16 phases for DLAA; ceil(16/scale^2) for lower resolutions.
- Guarded engine hooks: executable hash above, config getter RVA47ca0,
  base dependency RVA1724d0, SMAA vtable RVAb25ed8, jitter globals RVA116c71c,
  frame owner RVA51d40, frame getter RVA51b10. Full hash plus entry signatures.
  Render scale getter overrides VirtualScreenScale; mip bias log2(scale).
- Live DLAA process19144: 3840x2160->3840x2160, thousands of NGX success frames,
  reset=0 after first frame, camera and character movement inspected.
  `dlaa-motion.mkv` and contact sheet in prior UkNMwk lab; no severe corruption
  observed. The F10 toggle portion still needs checking (no toggle evidence yet).
- Live Quality process18712: 2560x1440->3840x2160, at least5400 successful
  evaluations with retained history; full-resolution HUD visible. Game closed
  before later screenshot; do not misinterpret capture-window failure as INI error.
- Currently Mode=Quality installed; project INI default now matches.
- New writable lab after credit reset: sandbox-XuOCTF/elex-dlss-lab. Earlier
  captures, screenshots and recordings remain in UkNMwk and IsZdaa labs.
- Still needed: final Quality motion/menu/toggle validation, clean shutdown,
  documentation/field note, distribution package, original-mod/game-content lint.
  Balanced/Performance, extensive cutscenes and live resolution changes unverified.

## Final live validation and packaging (2026-11-02 morning)

- Latest compiled/installed DLL SHA256:
  1bd71fd9adfdbf8801448e86684ba256147064cbebbab650a2d17d10629af502.
  Added F10 high-bit rising-edge polling, atomic cross-thread enable flags and
  preservation of the prior ranged PS constant-buffer binding.
- Process15880, Quality: >16000 successful evaluations 2560x1440->3840x2160,
  reset=0 across continuous frames. F10 disabled/enabled log lines confirmed;
  evaluation resumed. Pause menu/resume, camera pans and walking inspected.
- Quality demo captured with game-process-only audio and inspected contact sheet.
  Current private lab: sandbox-XuOCTF/elex-dlss-lab; quality-demo.*.
- Test closed normally through CloseMainWindow; no new ELEX dumps found. Save
  file timestamps remain pre-test (Steam's cloud metadata changed naturally).
- Final hardware/WARP smoke test passed, unrelated CS output42 and swap-chain
  creation verified. No editor errors. Source INI default now Quality; installed
  config beside the executable is authoritative, documented explicitly.
- Remaining validation limits are documented, not presented as verified:
  Balanced/Performance, extensive scenes/cutscenes, live resolution changes,
  device recovery, other GPUs/drivers, and quantitative performance benchmarks.

## Deliverables complete

- `dist/ELEX-DLSS-0.1.0-Windows-x64.zip`, source ZIP and SHA256SUMS created.
- Binary archive contains only the mod, official NVIDIA runtime, configuration,
  documentation, licenses and installer/rollback/mode scripts. No game binaries,
  shaders, saves or captures. Archive integrity verified.
- Universal Modder source-package lint:48 files, zero failures/warnings.
- Binary-package lint against live install flags exactly the two installed mod
  DLLs as self-matches. Both were verified against the installation manifest.
  Comparison with the preserved pre-install system snapshot:12 files, zero
  failures/warnings. No original ELEX game files in either package.
- Local field note validated with zero failures/warnings; not published.
- `dist/ELEX-DLSS-Quality-Demo.mp4`:21.2 seconds,1920x1080/30fps, game-only audio,
  labeled native D3D11 Quality1440p->4K. Finished contact sheet inspected.
- Final installed configuration is `system/elex-dlss.ini`, Mode=Quality. Source
  template matches; README explains that editing it does not update the install.
- Rollback dry run passed. Game closed normally after final checks. No active
  gameplay test or required approval remains pending.
