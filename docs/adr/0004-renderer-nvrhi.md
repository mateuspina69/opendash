# ADR 0004 — NVRHI (D3D12) renderer with pluggable upscalers

Date: 2026-10-08. Status: accepted. Supersedes ADR 0002.

## Context

OpenDash is a modern port. Real lighting on de-lit textures and ray tracing are planned. ADR 0002's D3D11 with
runtime `D3DCompile` cannot reach SM 6.x / DXR. The user wants quality anti-aliasing (DLAA) without locking the
port to one GPU vendor.

## Decision

- **RHI:** NVRHI (vcpkg port) on its D3D12 backend. A Vulkan device can be added later without touching the
  `od::Renderer` interface.
- **Shaders:** compiled at build time with DXC (vcpkg `directx-dxc`, row-major) into headers
  (`cmake/shaders.cmake`), each at the lowest shader model it needs: SM 6.0, and SM 6.5 for the ray-query scene
  pixel shader (used only when the device reports SM 6.5 and ray queries). SM 6.6 would need the Agility SDK on
  Windows 10 and newer drivers; with it, every pipeline failed to create on such machines.
- **Scene pass** at render resolution. Targets:
  - colour RGBA16F;
  - motion RG16F (pixels, previous − current, from unjittered matrices);
  - normal RGB10A2 (flat face normals, floors +Y);
  - reactive R8 (semi-transparent ranges, 2D sky);
  - depth.
- **Depth** stays as in ADR 0002: D24S8, standard Z, `LESS`, near 8 / far 200000. Reverse-Z on D32F was tried. The
  room data has coplanar faces whose depths differ only by rounding (different quad splits): D24 quantisation
  ties them like the original, D32F showed them as stripes. Upscalers get an R32F copy of this depth.
- **Motion history** is per actor. Dynamic geometry is uploaded as `DynamicObject`s with stable ids
  (`kind << 24 | index`). The same ids drive the 30 Hz tick interpolation. `resetHistory()` on room change,
  teleport or stage switch also resets the upscaler.
- **`Upscaler` interface** with these backends:
  - DLSS/DLAA (NGX direct, SDK 310.9.1);
  - FSR (FidelityFX SDK 2.3.0, ffx-api);
  - XeSS 3.0.2;
  - NIS 1.0.3 (spatial);
  - none.
- **AA selection:**
  - `auto` = DLSS, else FSR, else none;
  - `opendash.ini` `aa=` / `aa_quality=`, `--aa` / `--aa-quality`, F1 combos.
- **Signs:** jitter and motion-vector signs were chosen per backend by PSNR against the no-AA image (static and
  moving ST09 shots).
- **Vendor SDKs:** fetched into `third_party/`. Every build compiles all four backends, so the same sources always
  give the same exe. Whether a backend is offered is decided only at run time: vendor DLLs are delay-loaded or
  loaded by the SDK, so a missing DLL, or a GPU or driver the backend does not support, turns the backend off
  instead of stopping the exe.
- **UI:** ImGui and the message windows are drawn after upscaling at output resolution, by our own ImGui backend
  (`imgui_nvrhi`).

## Consequences

- Windows / D3D12 only until a Vulkan device is added.
- Updating a vendor SDK is a deliberate change to plus the sign check.
- Lighting and ray-tracing passes slot in between the scene pass and the upscaler, using the normal and depth
  targets.
  - Model normals have mixed orientation (inconsistent PS1 winding), so lighting must treat them as two-sided or
    repair the winding.
- Redistribution needs the vendor licence texts:
  - NVIDIA RTX SDK licence (DLSS);
  - MIT (FidelityFX, NIS);
  - Intel Simplified Software License (XeSS, binary only, notice reproduced).
- Verification:
  -  against `tests/golden/render/` (D3D11 references, `--aa none`);
  - `--motion-selftest`;
  - `--resize-test`.
