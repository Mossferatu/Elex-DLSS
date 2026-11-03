# Changelog

## Unreleased

- Native NVIDIA Dynamic Multi Frame Generation through a D3D11/D3D12 presentation
  bridge, with Reflex and configurable output FPS (default122), without a multiplier ceiling.
- Fix fullscreen startup by refreshing flip-model buffers after ELEX's mode switch.
- Preserve independent SR/FG settings; package production Streamline runtimes and
  licenses; verify their downloads and include them in installer/rollback checks.
- Build and attach Windows/source ZIPs and checksums when a GitHub release is published.
- Pinned toolchain/SDK downloads, package verification and manual build-only runs.
- Derive archive and DLL versions from release tags, including prerelease tags.
- Support clean ELEX installations, default-only user configuration and rollback
  while retaining conflict checks for existing graphics wrappers.

## 0.1.0 - 2026-11-02

- Native NVIDIA NGX D3D11 Super Resolution and DLAA at ELEX's SMAA T2x resolve.
- Pre-HUD scene input, native depth and motion, character-jitter normalization.
- Live renderer support for deferred command lists and 752-byte camera constants.
- Executable-guarded internal render scale, texture mip bias and Halton jitter.
- Reliable F10 toggle, history reset and D3D11 context-state restoration.
- Existing cloud/water corrections retained in the combined DLL.
- Live verification of 4K DLAA and 1440p-to-4K Quality on RTX 5090 / 621.01.
- Backed-up installer, update/mode helpers and hash-checked rollback.
