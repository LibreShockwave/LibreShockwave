# Emulator Architecture Documentation

This document set explains how LibreShockwave currently emulates a Director movie at runtime. It is based on direct inspection of the repository's implementation in `sdk`, `vm`, `player-core`, and `player-wasm`, not on external product literature.

That distinction matters because the project is still under active development. The goal of these documents is to describe what the emulator does today, which parts are strongly evidenced by code, and which parts still depend on compatibility assumptions.

## Document Set

- `emulator-overview.md`
  - End-to-end runtime architecture, startup, frame execution, score traversal, and event order.
- `file-loading-and-decoding.md`
  - How movie bytes become a `DirectorFile`, how Afterburner-compressed content is unpacked, and how bitmap/text resources are decoded and cached.
- `rendering-pipeline.md`
  - How sprites become baked drawables, how stage composition works, and where inks, text, shapes, and film loops are processed.
- `vm-memory-and-execution.md`
  - The Lingo VM execution model, handler dispatch, safety limits, memory ownership, and cache invalidation.
- `io-audio-network-and-platform.md`
  - Input, hit testing, editable text behavior, networking, external cast loading, audio, xtras, and the WASM platform bridge.
- `cast-libraries-members-and-items.md`
  - Cast library loading, dynamic members, sprite/member rebinding, item metadata, and how `.props`-style resources fit into the runtime.

## Recommended Reading Order

1. Read `emulator-overview.md` first.
2. Read `file-loading-and-decoding.md` for the asset and parser side of the emulator.
3. Read `rendering-pipeline.md` if the concern is visual output.
4. Read `vm-memory-and-execution.md` if the concern is Lingo behavior, state lifetime, or cache invalidation.
5. Read `io-audio-network-and-platform.md` for browser, desktop, or external I/O behavior.
6. Read `cast-libraries-members-and-items.md` for furniture, props, dynamic members, and cast resolution rules.

## Scope Notes

- These documents focus on the runtime emulator, not the Swing editor in `editor/`.
- The descriptions are implementation-oriented rather than aspirational. If code and historical Director expectations differ, the docs describe the code path that exists in this repository.
- When a subsystem has broad coverage from concrete code paths and tests, its confidence score is higher.
- When a subsystem depends more on runtime integration, movie-specific scripts, or partially implemented compatibility behavior, its confidence score is lower.

## Confidence Score

- Emulator architecture: `9.3/10`
- File loading and asset decoding: `8.9/10`
- Rendering pipeline: `9.0/10`
- VM and memory model: `8.8/10`
- I/O and platform bridge: `8.8/10`
- Casts, members, and item handling: `8.9/10`

Reason for score: the main execution path is well evidenced by direct inspection of core classes such as `Player`, `FrameContext`, `StageRenderer`, `FrameRenderPipeline`, `DirectorFile`, `AfterburnerReader`, `LingoVM`, `CastLibManager`, `NetManager`, `SoundManager`, `MultiuserXtra`, and the WASM bridge classes. Confidence remains lower at edges where compatibility depends on movie-specific scripts or asynchronous host behavior.
