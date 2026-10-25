# ADR 0003 — Dear ImGui for development UI

Date: 2026-10-07. Status: accepted.

## Context

Exploring rooms needed restarting `opendash.exe --stage N --sub M` for every change. A navigable
menu needs text and widgets, which the renderer does not have.

## Decision

- **Dear ImGui** (MIT) from vcpkg, with the `sdl3-binding` and `dx11-binding` features.
- It is a **development tool**, not part of the game's own UI.
  - F1 opens the stage / sub-room browser.
  - PageUp / PageDown and `[` / `]` step through stages and sub-rooms.
- The renderer exposes an overlay callback that runs before `Present`, plus its native device and
  context for the backend. The core library does not depend on ImGui.
- `imgui.ini` is disabled, so nothing is written next to the exe.

## Consequences

- One more vcpkg dependency, prebuilt and small. It works on every platform that SDL3 targets.
- Later debug tools reuse it:
  - camera controls;
  - actor inspector;
  - render toggles.
