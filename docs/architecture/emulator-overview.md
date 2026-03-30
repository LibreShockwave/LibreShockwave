# Emulator Overview

## 1. Runtime Shape

LibreShockwave is organized into a small number of clearly separated layers:

- `sdk`
  - Parses Director container data, score data, cast members, scripts, bitmaps, palettes, and text resources.
- `vm`
  - Executes Lingo bytecode, resolves handlers, exposes builtins, and tracks runtime scope, globals, deferred tasks, and error state.
- `player-core`
  - Owns the movie runtime: frame stepping, sprite state, cast libraries, rendering, input, sound, networking hooks, and timeout processing.
- `player-wasm`
  - Adapts `player-core` to a browser-style environment by replacing direct JVM services with queued bridges for networking, multiuser, audio, and frame export.
- `editor`
  - Tooling and inspection UI, not part of the core emulator path.

In practical terms, the emulator is not one monolith. It is a Director file parser plus a Lingo interpreter plus a stateful movie player plus a rendering pipeline.

## 2. What The `Player` Owns

`player-core` centers the runtime around `Player`. That object is the integration point for the rest of the system. It owns or coordinates:

- the parsed `DirectorFile`
- the `LingoVM`
- the `FrameContext`
- the `StageRenderer`
- the `FrameRenderPipeline`
- cast library management
- sprite and movie property bridges
- input state and input dispatch
- timeout processing
- audio playback
- networking or an injected network provider
- bitmap and text rendering services
- xtra management and optional multiuser support

This makes `Player` the authoritative runtime boundary. If something affects movie behavior at frame level, it almost always passes through `Player` or one of the objects it owns.

## 3. Movie Startup Sequence

Movie execution does not begin with the first frame draw. It begins with a staged preparation phase.

At a high level, startup works like this:

1. A `DirectorFile` is created from movie bytes.
2. `Player` is constructed around that file and the compatibility/profile services it needs.
3. `play()` transitions the player into active state.
4. If the player was stopped, `prepareMovie()` runs before normal ticking begins.

The current `prepareMovie()` sequence is important because it defines the runtime contract seen by Lingo:

- initialize builtin movie variables
- run compatibility-profile pre-start hooks
- preload all external casts that must already exist
- preload mode-2 casts
- dispatch timeout `prepareMovie`
- dispatch movie script `prepareMovie`
- preload mode-1 casts
- initialize the first frame
- dispatch `beginSprite`
- dispatch timeout `prepareFrame`, then global `prepareFrame`
- dispatch movie script `startMovie`, then timeout `startMovie`
- dispatch global `enterFrame`
- dispatch timeout `exitFrame`, then global `exitFrame`
- preload mode-1 casts again

This is not a generic "load assets and start" flow. It is a compatibility-shaped startup pipeline designed to match Director movie expectations around when data and handlers become visible.

## 4. Frame Execution Model

The steady-state runtime loop is driven by `Player.tick()`. Each tick performs a fixed set of steps:

1. Install providers and runtime bridges needed by this tick.
2. Arm the tick deadline used by the VM timeout logic.
3. Process queued input events.
4. Execute the current frame through `FrameContext.executeFrame()`.
5. Tick all xtras.
6. Run compatibility-profile post-frame hooks.
7. Process pending timeouts.
8. Call `update` on objects registered for periodic updates.
9. Advance to the next frame through `FrameContext.advanceFrame()`.
10. Clear temporary providers and flush deferred VM tasks.

This ordering matters. Input is applied before frame logic. Timeout processing happens after frame execution. Frame advancement is explicit, not implicit. Deferred work is flushed at the end of a tick rather than at arbitrary points.

## 5. Timing, Tempo, And Deadlines

The runtime has a clearer timing model than the surface API suggests.

Current tempo priority is:

- `puppetTempo`
- score tempo channel for the current frame
- player base tempo from configuration

This matters because authored movies can change pacing at different layers. The player also updates input caret blink timing from the effective tempo, so timing is not only about frame scheduling.

The VM additionally runs with a tick-level deadline when configured. `Player.tick()` arms that deadline before executing frame logic, and the VM can use it to stop runaway execution within the current frame.

## 6. Score And Frame Navigation

The score is not treated as a simple array of independently rendered sprites. It is interpreted through score navigation and sprite spans.

Current responsibilities split roughly like this:

- `ScoreNavigator`
  - Converts score channel data into frame intervals and active sprite spans.
- `FrameContext`
  - Tracks the current frame, pending frame jumps, frame script instance, and sprite lifecycle transitions.
- `StageRenderer`
  - Maintains runtime `SpriteState` objects for score-backed and dynamic sprites.

Important behaviors:

- channel 0 is treated as frame-script space
- sprite activity is span-based, not recomputed from scratch in a naive way each frame
- frame labels are resolved through the score metadata
- puppeted sprites are allowed to survive score transitions differently from purely score-owned sprites

The score therefore behaves as authored timeline data plus runtime override state, not as immutable playback instructions.

## 7. Event Order

Frame execution in `FrameContext.executeFrame()` follows a defined event order:

- actor list `stepFrame`
- global `stepFrame`
- actor list `prepareFrame`
- timeout `prepareFrame`
- global `prepareFrame`
- actor list `enterFrame`
- global `enterFrame`

Frame advancement then performs the exit side of the lifecycle:

- actor list `exitFrame`
- timeout `exitFrame`
- global `exitFrame`
- determine destination frame
- end sprites leaving the old frame
- switch current frame and frame script
- begin sprites entering the new frame

The actor list is not a side feature. It is a core part of event dispatch and participates alongside global handlers and frame scripts.

## 8. Script Propagation Model

Global events are not dispatched to one place. The runtime currently propagates them in layers:

- sprite behaviors in channel order
- frame behavior
- movie scripts

Propagation is intentionally pass-sensitive. A handler that does not call `pass()` can stop further propagation for that chain. That detail is central to Director compatibility because many authored movies depend on event interception semantics rather than purely declarative state.

Movie script lookup is also broader than "main file only". The VM can resolve handlers from the main movie and from loaded external cast libraries.

## 9. Runtime State Versus Authored State

One of the most important architectural facts in this emulator is that runtime state is intentionally richer than the authored score.

Each sprite channel can hold mutable runtime data such as:

- location, size, depth, visibility
- ink, blend, colors, rotation, skew, flips
- puppeting state
- cursor overrides
- dynamic member binding
- attached script instances

That means the movie is not rendered directly from score data on every tick. The score provides defaults and authored intervals, while runtime objects preserve mutations, dynamic members, and script-driven state across frames when appropriate.

## 10. Compatibility Profiles And Service Injection

The player has an explicit compatibility-profile seam. That profile can influence startup and per-frame behavior through hooks such as:

- before-prepare-movie work
- after-frame execution work
- external-cast loaded notifications
- handler enter and exit observation

The VM is also not hardwired directly to the player. Each tick, the player installs thread-local providers for:

- networking
- xtras
- movie properties
- sprite properties
- cast libraries
- timeouts
- update callbacks
- external parameters
- sound
- active palette resolution

That is a key architectural choice. It lets the VM stay mostly generic while still giving builtins access to the current runtime services.

## 11. Output Contract

The player's public render output is a `FrameSnapshot`. That snapshot is not just a pixel buffer. It carries:

- frame number
- stage dimensions
- background color
- baked sprite list
- stage image when present
- bake tick and pipeline trace metadata
- a human-readable title string

This is useful because the emulator can serve debugging, rendering, and host-platform display from the same frame-level artifact.

## 12. What This Means Operationally

The emulator should be understood as a deterministic frame machine with compatibility-specific hooks, not as a loose script host.

In practice:

- Director file parsing happens once, then feeds runtime objects.
- Lingo drives behavior, but only within the boundaries of the player tick and event order.
- Rendering is a downstream result of sprite state, not the definition of sprite state.
- Dynamic members and puppeted sprites let runtime behavior temporarily diverge from authored score data.
- WASM is an environment adaptation layer, not a separate emulator implementation.

## Confidence Score

- Emulator core structure: `9.4/10`
- Startup, timing, and frame lifecycle ordering: `9.3/10`
- Score and sprite ownership model: `9.0/10`
- Compatibility/service injection model: `8.9/10`

Reason for score: these conclusions come directly from the current `Player`, `FrameContext`, `ScoreNavigator`, `BehaviorManager`, `EventDispatcher`, and movie-property/provider integration code. Confidence is slightly lower around movie-specific edge cases because authored Lingo can still create unusual control flow or lifetime patterns that are only visible under full runtime execution.
