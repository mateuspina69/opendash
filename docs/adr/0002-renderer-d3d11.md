# ADR 0002 — Direct3D 11 for the M1 3D renderer

Date: 2026-10-06. Status: superseded by [ADR 0004](0004-renderer-nvrhi.md) (2026-10-08).

## Context

ADR 0001 left the 3D API open: SDL_GPU, or D3D11 if SDL_GPU blocks progress. SDL_GPU needs
pre-compiled shader bytecode per backend (DXIL / SPIR-V / MSL). That means adding a shader
toolchain (dxc / SDL_shadercross) to the build before the first triangle.

## Decision

- **D3D11** on the HWND of the SDL3 window, with HLSL compiled at runtime (`D3DCompile`, system
  `d3dcompiler_47.dll`).
- **Texturing:**
  - one `Texture2DArray` with one 256×256 slice per texture entry (tpage/CLUT) used by the room;
  - point sampling;
  - alpha test (PS1 colour 0 = transparent);
  - no back-face culling (PS1 strips have inconsistent winding).
- **Isolation:** the renderer stays behind a small interface (`render/`), so SDL_GPU or Vulkan can
  replace it later without touching the core.

## Consequences

- Windows-only for now, which is the platform of the original PC game.
- Shader compile errors surface at startup in the log rather than at build time.
