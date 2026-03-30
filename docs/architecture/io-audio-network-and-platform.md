# I/O, Audio, Network, And Platform Bridge

## 1. Input Is Queue-Based, Not Immediate

LibreShockwave does not directly mutate movie state from external mouse and keyboard callbacks. External input is first captured into `InputState`, then consumed during the next player tick by `InputHandler`.

That gives the runtime a stable contract:

- platform events arrive asynchronously
- movie logic observes them during controlled frame execution
- render and hit-test state stay aligned with the current tick

This is the right model for an emulator because it prevents random host timing from tearing apart frame semantics.

## 2. Mouse State And Keyboard State

`InputState` tracks more than raw cursor position. It currently carries state such as:

- mouse coordinates
- button down/up state
- click and double-click timing
- rollover target sprite
- keyboard focus sprite
- key state
- text selection state
- caret blink state
- queued input events

That means input is already modeled as a small subsystem, not as a handful of booleans.

## 3. Input Dispatch

During `Player.tick()`, `InputHandler.processInputEvents()` drains the input queue and dispatches the resulting behavior changes.

Important effects include:

- rollover transitions
- mouse-driven sprite targeting
- keyboard focus updates
- caret and editable text maintenance
- sprite revision bumps when input changes can affect visible interaction state

Because this happens before frame execution, scripts see a coherent input snapshot during the rest of the tick.

## 4. Hit Testing

Input depends on the most recent baked sprite set. `HitTester` uses stage-published render data to walk the visual stack front-to-back.

Current hit-testing behavior is intentionally pragmatic:

- default to Director-style bounds-based hit testing
- use per-pixel alpha testing only when the source data has trustworthy native alpha
- prefer the last baked render order instead of recomputing a second scene graph

This keeps input semantics aligned with display semantics without forcing the system into expensive per-pixel checks for every interaction.

## 5. Editable Text And Focus Behavior

The emulator includes built-in support for editable field behavior rather than leaving it entirely to host UI widgets.

Current functionality includes:

- focus acquisition when clicking editable text sprites
- text insertion
- backspace and arrow-key editing
- selection tracking
- caret updates
- tab-based focus navigation

This matters because editable fields are part of the movie runtime, not a browser form overlay.

## 6. Cursor And Platform Navigation Hooks

The movie-property layer also exposes host-facing hooks that are easy to miss if you only look at input events.

Important current examples:

- `the cursor`
  - stored as a movie property and interpreted by the host/UI layer
- `gotoNetPage`
  - delegated to an injected platform handler instead of hardwiring browser or desktop navigation into the VM

In WASM, navigation requests are queued back out through `WasmEntry` rather than performed directly inside the core player. That keeps the emulator side pure and lets the host decide how navigation should be handled.

## 7. Networking

Networking is represented through an abstract provider shape with multiple concrete integrations.

### 7.1 JVM Path

On the JVM side, `NetManager` handles Director-style asynchronous network operations. It supports behaviors such as:

- HTTP GET and POST
- async completion tracking
- result text and error status reporting
- cached bytes for repeated resource access
- integration with external cast loading

The runtime exposes Director-flavored status queries such as `netDone`, `netTextResult`, and stream-status inspection rather than a raw Java networking API.

### 7.2 WASM Path

In WASM, direct JVM-style networking is replaced by `QueuedNetProvider`. The Java side queues work requests and the JavaScript host fulfills them asynchronously, returning results back into the emulator.

Architecturally, this means:

- `player-core` logic remains mostly unchanged
- the environment boundary moves to a queue/bridge layer
- pending request state and byte progress are still visible to the movie runtime

The current WASM network bridge is more detailed than a simple request queue:

- authoring-machine paths are normalized into host-usable filenames or URLs
- root-relative URLs can be resolved against the server origin
- fetched data is cached by filename and basename
- `bytesSoFar` is advanced during polling so Director-style download scripts do not assume the fetch stalled

## 8. External Cast Loading

External cast libraries are not treated as a separate download system. They are integrated into the same network/resource pipeline.

The effective flow is:

1. a cast library identifies an external file dependency
2. bytes are requested through the active network provider
3. completion is reported asynchronously
4. the player installs the cast contents into runtime state
5. render, handler, and member caches are invalidated where needed

This is an important design choice because it keeps resource fetch and runtime visibility tightly coupled.

## 9. Audio

Audio is handled by `SoundManager`, which acts as a Director-aware coordinator instead of pushing sound directly from scripts to the host platform.

Current model:

- eight sound channels
- cast-member resolution into sound/media payloads
- decoding support for supported encoded formats
- delegation to a pluggable audio backend for actual playback

The current desktop-side implementation is more concrete than that summary suggests:

- `sound(N).play(...)` can accept a member reference, prop list, or playlist-like list
- per-channel volume is stored in Director's 0-255 range
- sound members are resolved through cast libraries back to `SoundChunk` or `MediaChunk`
- audio is converted into playable WAV or MP3 bytes before backend handoff

In WASM, audio is explicitly command-queued because the worker-side runtime cannot directly control Web Audio on the main thread. The backend tracks:

- pending play, stop, and volume commands
- loop count
- current playing state per channel
- stop notifications coming back from JavaScript

This division is sensible because movie logic should reason about Director channel semantics while the backend handles host-specific playback details.

## 10. Xtras And Multiuser

Xtras are first-class runtime participants. The player ticks xtras explicitly each frame through `xtraManager.tickAll()`.

Multiuser behavior is bridged through platform-specific networking helpers:

- JVM uses a socket-backed bridge
- WASM uses a queued bridge that communicates with JavaScript

The associated VM-side xtra exposes Director-style methods for:

- connecting
- sending messages
- receiving queued messages
- polling message availability

The current Multiuser implementation also auto-processes callbacks when a message handler has been registered. That means message delivery is not limited to explicit polling by movie scripts.

Platform differences are concrete:

- the desktop bridge opens sockets on a background thread and polls incoming bytes without blocking the frame loop
- the desktop bridge currently treats each available UTF-8 chunk as one message payload
- the WASM bridge queues connect, send, and disconnect requests for JavaScript and receives synthetic connection, error, and message events back into the runtime

This is a good example of the project's general architecture: keep the movie-facing API stable, swap the host adapter underneath.

## 11. WASM Boundary

`player-wasm` does not reimplement the emulator. It wraps the same runtime core with environment-specific adapters.

Important traits of the WASM boundary:

- JS-facing exported entry points live in the WASM wrapper layer
- frame output is copied through shared memory buffers
- networking and multiuser traffic are queued across the host boundary
- audio is delegated through a bridge backend
- a software renderer can cache frame output across revisions

The current WASM entry layer is also intentionally one-way from the emulator's perspective:

- exported functions are the public surface
- there are no direct JS imports in the computation core
- debug output, trace hooks, navigation requests, and async resource delivery all move through explicit queues or shared buffers

So the browser build is best described as "the same emulator with different services", not as a second, unrelated player.

## 12. Practical Summary

The I/O model is disciplined:

- inputs are queued
- interactions are resolved against rendered state
- network access is async and Director-shaped
- external casts ride the same resource pipeline
- audio is channel-aware and backend-pluggable
- WASM stays close to `player-core` by replacing services instead of forking behavior

That architecture should make the emulator easier to keep consistent across desktop and web builds.

## Confidence Score

- Input and hit-testing model: `9.0/10`
- Network and external resource flow: `8.9/10`
- Audio and xtra platform integration: `8.8/10`
- WASM bridge model: `9.0/10`

Reason for score: the structural design is clear from `InputHandler`, `InputState`, `HitTester`, `MovieProperties`, `NetManager`, `QueuedNetProvider`, `SoundManager`, `WasmAudioBackend`, `MultiuserXtra`, `SocketMultiuserBridge`, `WasmMultiuserBridge`, `WasmPlayer`, and `WasmEntry`. Confidence remains slightly lower where behavior depends on asynchronous host integration rather than purely local runtime code.
