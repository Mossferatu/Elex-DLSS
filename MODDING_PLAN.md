# ELEX native DLSS plan

Native x64 D3D11 shader/engine hooks for Steam build 2617898 on RTX 5090.
The existing cloud/water fix must remain functional. Community data tooling
(Baltram/rmtools) does not supply a temporal renderer integration API.

1. Inspect existing RenderDoc captures and record exact pass/shader identities.
2. Prove the temporal input contract: scene color before UI, depth conventions,
   per-object motion, camera jitter and history/reset conditions.
3. Build and exercise an NGX D3D11 backend with validated input contracts.
4. Connect the backend at a verified engine pass. DLAA can validate native-size
   reconstruction before lower-resolution rendering, mip bias and HUD handling.
5. Back up current game binaries/config/saves, then install a reviewed build.
6. Validate in live gameplay and record evidence; package only original code
   and dependencies permitted for redistribution. Keep a rollback manifest.

Success means real, temporally stable DLSS evaluations integrated with gameplay;
a proxy, diagnostic probe or screenshot-space filter alone is not completion.

Unknowns: whether ELEX provides usable object motion vectors and camera jitter;
exact upscaling insertion point; scene/HUD resolution separation; native dynamic
resolution support; compatibility with the existing water function detours.

Reference sources:
- https://github.com/Baltram/rmtools
- https://github.com/NVIDIA/DLSS
- https://renderdoc.org/

No installation or graphics-settings changes have been made yet.
