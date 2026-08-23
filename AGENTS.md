# AGENTS.md — LibreShockwave Working Guidelines

This file is the working guide for this repository: how to approach bugs, where fixes
must live, how to verify them, and how to use the desktop debugger, its recordings,
and both network transports. Read it before starting an investigation and keep it
current after meaningful changes.

## Engineering Principles

Write for a capable developer who never saw this code: they must be able to read,
fix, and extend it without asking.

- Prefer clarity over cleverness. Explicit beats implicit; minimize magic. If a
  trick needs explaining, write the plain version.
- Complexity budget: small functions, cohesive modules. No hidden globals — state
  flows through the `Player`, the debugger contexts, and the provider/bridge
  objects via constructor injection and explicit setter wiring; there is no
  process-wide singleton, keep it that way. External parameters travel explicitly
  (`Player::setExternalParams`, the debugger parameters dialog, recording
  headers) — never through globals. Shared mutable state only where the
  architecture already demands it (the cross-thread queues between the VM thread
  and the debugger UI).

## Hard Rules

These are hard rules, set by the project owner:

1. **C++ naming convention**
   - Types (class/struct/enum/alias/concept): `PascalCase`
   - Functions, methods, variables, members: `snake_case`
   - Private members: trailing underscore (`read_buffer_`)
   - Namespaces: `snake_case` (`libreshockwave::player::net`)
   - Files: `PascalCase.hpp` / `PascalCase.cpp` matching the existing tree
     (`NetManager.hpp`); test files use `snake_case_test.cpp`
     (`sdk_foundation_test.cpp`)
   - Macros: `SCREAMING_SNAKE_CASE` — avoid macros altogether where possible

2. **No duplicate functions** — every piece of logic has exactly one canonical
   implementation. Shared logic is extracted into a single free function owned by
   the appropriate module; never copy-paste-adapt.
3. **No trivial pass-through methods** — do not write a method whose body only
   calls one other method and returns its result; call the target directly
   instead. Only allowed when the wrapper adds a contract: synchronization,
   caching, a stable public API over changing internals, or virtual dispatch.
4. **One responsibility per file, under 600 lines** — one primary class/component
   per header/source pair; every file stays under 600 lines (hard limit). When a
   file approaches it, split responsibilities — do not append. Keep only a few
   files per folder (roughly five or fewer); when a module outgrows that, split it
   into subfolders by concern (as `player/net/`, `player/xtra/`, and
   `lingo/vm/datum/` do) and mirror the layout under
   `cpp/include/libreshockwave/`, `cpp/src/`, and `cpp/tests/`.
5. **Modern C++** — RAII; const-correctness; `[[nodiscard]]` on anything
   returning an error/status or a new object; `std::span` instead of raw
   pointer+length pairs; include-what-you-use; forward declarations in headers;
   `#pragma once`.
6. **Error handling** — exceptions only for programmer errors and fatal startup
   failures. Runtime I/O, parsing, and network errors use `std::error_code` or
   `std::optional`/result-style returns, following the style of the module being
   touched. Exceptions never cross thread boundaries (VM thread ↔ debugger UI).
7. **Formatting** — 4 spaces, braces open on the same line as the statement and
   close on their own line, 100-column limit, `const` before the type
   (`const int`), `auto` only where it removes noise (iterators, `make_unique`),
   one declaration per line.
8. **No band-aid fixes** — root-cause fixes only. Never ship shims, workarounds,
   or half-assed patches that paper over a symptom. If the correct fix turns out
   larger than expected, do it properly anyway (splitting files as rule 4
   requires); if unsure of the right approach, ask Alex instead of committing a
   hack.
9. **Tests for every function added** — when a function is added or changed, add
   or update unit tests covering it in the same change, so no regression slips
   through. Tests double as documentation: a reader can learn the API call
   patterns from them, so cover usage of the public surface. Cover edge cases
   too — empty/zero/largest inputs, malformed frames, error paths — not just the
   happy path. A task is not done until the new tests are in place and the full
   suite passes (`ctest --test-dir cmake-build-debug --output-on-failure`).
10. **No unsolicited docs files** — do not create new files under `docs/` (or new
    `.md` files anywhere) unless the user explicitly asks or an approved plan
    calls for them.
11. **Comments are max 2 lines** — no multi-line comment blocks: a comment
    (single `//` line or a `/* */` block) may span at most 2 consecutive lines.
    Prefer one short line; say what is non-obvious, never restate the code.
    Exception: public-API doc comments in headers (rule 12) are exempt from the
    2-line cap; inline comments inside function bodies are not.
12. **Docstrings on every public API** — every public type and function in
    `cpp/include/libreshockwave/` carries a brief `//` block comment directly
    above it: what it does, its preconditions and contracts, and a short usage
    example for non-obvious APIs (parsers, result-style returns, live-bitmap
    wiring). Match the surrounding file existing comment style; do not introduce
    doxygen-style markup. Tricky private logic gets an inline comment at the
    definition, within the rule 11 cap.
13. **No magic numbers** — every literal falls in exactly one bucket:
    - Director/Shockwave format constants (chunk tags, version semantics,
      score/frame rules, SMUS framing): named constants, never configurable —
      never move these to external parameters.
    - Tunables (timeouts, buffer sizes, throttle caps, frame budgets): named
      file-local constants; a value that must vary per deployment comes through
      the external parameters — never an inline literal.
    - Asset-owned values (Lingo-visible rules, limits): a file-local named
      constant plus a one-line source comment marking it as asset-owned or
      deferred.
    Style: file-local `constexpr` with `kCamelCase` names
    (`kRecordingFlushIntervalMs`, `k_listener_tag`).
14. **Logs are actionable** — every diagnostic line carries identifying context
    in a message prefix (`connection 5: ...`, `event loop 1: ...`) and states
    what happened and what it means, so the line alone suggests the next action.
    Handle every error — never swallow silently; log each one once, at the right
    level, where it is handled. Use the existing diagnostic channels (debugger
    output handlers, status bar, WASM debug-message poll); do not invent a new
    logging framework.
15. **Security** — validate all external input at the boundary: Director chunk
    and frame lengths, multiuser/SMUS frame sizes, network results; parsers
    reject malformed input rather than pushing the check onto callers. Secrets
    and credentials come only from external parameters or the environment —
    never in code, tests, or the repo. The connection presets in the debugger
    parameters dialog are local defaults, never committed credentials.
16. **Deterministic where feasible** — tests use fixed RNG seeds and never depend
    on wall-clock time (inject or parameterize time instead, as the debugger
    replay clock does). Never iterate a `std::unordered_map` and depend on its
    order.

## Git

- Commit messages: subject + body as the work warrants.
- **No `Co-Authored-By:` or any machine-attribution trailers.**
- Commit messages must not mention game, asset, room, user, or endpoint names.
  Describe the generic runtime behavior being fixed.

## Working Principles

Repository-specific constraints on top of the hard rules:

- Treat app-authored Lingo as app-authored Lingo. The runtime must not grow C++
  branches keyed to parent-script names, movie-script names, handler names,
  Resource API helper names, or room/login script names.
- No page-specific, room-specific, member-name-specific, or user-name-specific
  fixes. Keep fixes in the C++ runtime, VM, Xtra, networking, cast/member, input,
  or renderer layer that owns the actual failing behavior.
- Before treating a helper as a missing builtin, search the exported Lingo and
  asset text first. Some helpers that look like builtins are authored
  movie-script APIs; for example, `memberExists` is defined in
  `MovieScript 9 - Resource API.ls` under `v31_assets` and delegates to the
  Resource Manager.
- Canonical examples: `BigInt` / `HugeInt` / `HugeInt15`. Do not implement native
  C++ branches for `BigInt`, `HugeInt`, `HugeInt15`, `powMod`, `Modulo`, `div`,
  `getString`, or `getByteArray`. The same rule applies to hot asset handlers
  such as `getmemnum`, `memberExists`, `updateProcess`, `createPassiveObject`,
  `solveMembers`, and `parseCallback`.
- Do not restore C++ hardcoding for `pProcList`, `registerProcedure`, or
  `removeProcedure`.
- Do not seed synthetic broker instances with asset-owned state (proc-list
  templates, fixed event maps). If broker support is needed, preserve and
  dispatch real authored broker script instances and their properties.
- Performance improvements must be generic VM/runtime work: Director semantics,
  bytecode dispatch, handler dispatch, list/property-list behavior,
  script-instance properties, arithmetic, strings, builtins, data structures,
  cooperative scheduling, or cache behavior.
- Search with `rg` before assuming a behavior is missing. Read the relevant dated
  file in `docs/goals/` before drafting a new investigation.
- When an issue reproduces only in a specific movie, inspect the extracted `.ls`,
  `.lasm`, `.window`, `.props`, `manifest.tsv`, and bitmap/text exports before
  editing C++.
- If a diagnostic hardcoded path is used briefly to prove a cause, remove it
  before finalizing. The permanent fix must obey the generic-runtime rules
  above.

## Repository and Reference Paths

Primary repository: `/opt/git/LibreShockwave`

Nearby asset and reference repositories:

- `/opt/git/v31_assets` (mirror: https://github.com/Quackster/v31_assets/) — the
  main local source for v31 cast exports and Lingo dumps. Layout: `bitmaps/`
  (decoded PNG members), `text/` (STXT/XMED payloads, including `.window`,
  `.props`, `.index`, and config-like text members), `sounds/`, `palettes/` (TSV
  files), `raw_chunks/` (member-owned raw chunks), `manifest.tsv`, `file_info.tsv`,
  and `projectorrays_lingo/<file>/casts/**/*.ls` (ProjectorRays Lingo dumps).
- Frequently relevant paths under `v31_assets`:
  `/opt/git/v31_assets/hh_interface.cct`,
  `/opt/git/v31_assets/projectorrays_lingo/fuse_client`, and the `HugeInt15`,
  `Login Handler Class`, `Connection Instance Class`, `Multiuser Instance Class`,
  and `Event Broker Behavior` scripts under
  `projectorrays_lingo/*/casts/External/`.
- `/opt/git/v14_assets` and `/opt/git/v1_assets` — same extraction shape as
  `v31_assets`, useful for comparing older behavior and Director extraction
  output.
- `/opt/git/v1_assets/handshake-demo` — a minimal Node.js TCP demo server for the
  v1 EnterpriseServer-style handshake. It only gets the client out of the
  connection-wait loop; it does not implement login, rooms, messenger state, or
  gameplay.
- `/opt/git/test/LibreShockwave` — older investigation notes (e.g. private-room
  wall rendering). Key lesson: trace sprite references and authored
  initialization paths before changing broad rendering behavior.

Native visual references (evidence and comparison targets only; do not encode
their scene or member names into generic runtime behavior):

- `/home/alex/Pictures/Screenshots/Screenshot_20260615_215344.png`

Authored call paths that are useful for diagnosis but must stay authored Lingo:

- Bottom-left head icon: `updateEntryBar` → `createMyHeadIcon` →
  `Figure_Preview.createHumanPartPreview` → `getHumanPartImg(#head, ...)` →
  `Human Class EX.getPartialPicture(#head, ...)`.
- Welcome Lounge networking/performance: `Connection Instance Class.xtraMsgHandler`,
  `Login Handler Class.responseWithPublicKey`, `Login Handler Class.handleServerSecretKey`,
  `HugeInt15.powMod`, `HugeInt15.Modulo`, `HugeInt15.div`, `HugeInt15.getString`,
  `HugeInt15.getByteArray`.

Fix the generic runtime behavior underneath these paths.

## Building and Testing

Relevant build targets:

```bash
cmake --build cmake-build-debug --target libreshockwave_tests -j2
cmake --build cmake-build-wasm --target libreshockwave_cpp_wasm_dist -j2
```

Run the test suite with `ctest --test-dir cmake-build-debug --output-on-failure`.

- Do not create new CMake test executables or new top-level test files. All
  runtime coverage goes into `cpp/tests/sdk_foundation_test.cpp` and all
  desktop-debugger coverage into `debugger/cpp/debugger_test.cpp`; add test
  functions there and link any newly needed sources into those existing
  targets. New suites only fragment `ctest` output and bloat configure time.
- The full native test binary has a known pre-existing abort around a
  `sdk_foundation_test.cpp` `gcCallbacks == 1` assertion. If it still appears,
  document the exact assertion and run the focused tests that cover the current
  change.
- When serving the WASM distribution (usually from
  `/opt/git/LibreShockwave/cmake-build-wasm/cpp/wasm-dist`), restart the static
  server after rebuilding so deleted old bundle file descriptors cannot confuse
  the run.

## Rendering Rules

Validated C++ renderer behavior. Treat these as rules for future fixes and tests,
not as a complete rendering specification.

### General Approach

- Prefer root-cause fixes in image storage, VM image methods, ink handling, and copy
  paths over post-render pixel repair. Keep fixes in the C++ runtime, renderer, VM,
  and cast/member layers; do not compensate with client-side shims or hardcoded
  Lingo implementations.
- Keep indexed-image metadata and rendered ARGB pixels consistent: if a path changes
  visible pixels on an indexed image, it may also need to update `paletteIndices`.
- Preserve authored transparency and masks. Avoid broad heuristics that turn ordinary
  light pixels into transparent pixels.
- When changing one rendering path, rerun focused tests for adjacent behavior:
  palette copies, matte/background-transparent ink, masks, quad transforms, and text
  or color remap.
- If a visual regression appears in a script-created UI surface, inspect the baked
  `RenderSprite`, its dynamic `CastMember`, and the member runtime bitmap before
  changing matte heuristics. A missing sprite can be a bake-source selection problem,
  not a transparency problem.

### Image Creation and Palette State

- `image(width, height, depth[, palette])` starts as opaque white.
- For indexed images, attach the palette before the initial white fill so the
  backing pixels and palette indices are initialized together.
- `fill(..., paletteIndex(n))` on indexed images must update both the ARGB pixel
  data and `paletteIndices`.
- Integer palette-index fills on images with depth `<= 8` are indexed fills.
- RGB fills on 32-bit images that carry a palette reference resolve RGB normally
  without creating an indexed image.

### Palette Index Preservation

- Preserve source palette indices when copying from an indexed source into a
  compatible indexed destination for full-blend `COPY`, `MATTE`, and
  `BACKGROUND_TRANSPARENT` operations with no mask or remap.
- For `BACKGROUND_TRANSPARENT`, transparent source pixels must not overwrite the
  destination pixel or its destination palette index.
- When copying RGB or non-indexed pixels into an indexed destination that already
  has palette indices, refresh indices only inside the copied rectangle through the
  destination palette. Do not clear the entire index plane because part of an
  indexed destination received RGB pixels.
- When copied RGB white lands on an indexed destination, keep an existing index `0`
  only when the destination already had index `0` and palette entry `0` is white.
  Otherwise choose the nearest non-matte palette index so ordinary white text or
  panel pixels do not become holes in later matte operations.
- Scaled copies must scale palette indices with pixels when indices are preserved.

### Quad Copying

- Quad destinations use this point order: top-left, top-right, bottom-right,
  bottom-left.
- Axis-aligned quads cover identity, flips, and 90-degree transforms.
- Preserve transformed source palette metadata and indices when available.
- The fast quad path may handle plain `ink` and `blend`. Prop lists containing
  masks, darken or lighten behavior, remaps, or other copy properties should fall
  back through rectangular `copyPixels` so the existing property semantics remain
  active.
- Do not route all quads through the rectangular path. Landscape and mask
  transforms depend on the quad path keeping its transform behavior.
- Keep landscape and mask regression coverage when touching quad-copy, mask, matte,
  or background-transparent paths.

### Matte and Background-Transparent Ink

- `MATTE` ink uses a flood-fill matte from border-connected background pixels.
- If valid palette indices exist, prefer palette-index matte detection. Border
  index `0` white is normal matte/background when present and the image is not
  uniform.
- For script-built indexed UI buffers, sprite-level `backColor` is still meaningful
  for final `MATTE`. If it resolves to white through the active palette or as packed
  RGB, prefer the matching indexed white matte before the dominant-edge heuristic.
  This prevents dark shadow art on the edges from being mistaken for the matte color
  while the original white backing remains visible.
- Without valid indices, use the RGB flood-fill fallback, usually keyed from white.
- `BACKGROUND_TRANSPARENT` ink defaults the background key to white unless `bgColor`
  provides a different key.
- `BACKGROUND_TRANSPARENT` copies from indexed sources with the default white key
  should skip the authored matte index when palette index `0` is near-white, but
  must not erase duplicate white or near-white RGB pixels that came from other
  palette entries.
- Background-transparent copies should treat border-connected key pixels as
  transparent while preserving enclosed key-colored content when required.
- Script-built runtime backings under `BACKGROUND_TRANSPARENT` need provenance from
  the Lingo window framework plus copy geometry and source content. Do not treat
  every white `image.fill()` as backing that should survive final baking. In the
  fuse_client framework, `Image Wrapper Class.feedImage()` is the path that fills
  `pBuffer.image` and then renders a source image into it. Only that transaction may
  mark a possible preserved fill backing.
- Not every `feedImage` result should preserve white. Navigator text/link surfaces
  are also `#type: "image"` wrappers fed with writer-rendered images; their white is
  a matte and must still key out. Preserve the wrapper fill only when the source is
  a script-created rectangular image copied with `BACKGROUND_TRANSPARENT`, the
  requested source rectangle extends beyond the source image, and the visible
  source looks like sparse viewport/list content rather than a text/link matte or
  artwork slot. Full-size catalogue artwork, product strips, Navigator text/link
  mattes, untouched `image()` default white, generated text mattes, and
  room/compositor surfaces must stay on the normal white-keying path.
- Near-white matte preprocessing must stay narrow. Apply it only when the source
  has a border-connected near-white matte and real non-near-white content. A broad
  near-white rule can erase legitimate light text, highlights, and overlays.
- Outlined-white-body preservation is not a general script-buffer rule. Keep it for
  authored low-depth assets and explicitly scoped script-built 32-bit chat
  backgrounds. Do not apply it to script-modified indexed window buffers, where
  white is usually the matte backing.

### Window Buffers and Grouped UI

- Window layouts group repeated element IDs into one bitmap buffer per group. In
  `habbo_simple.window`, `shadow` is built before `back`; the shadow group uses
  mixed item inks with a shared blend, so the final sprite stays `MATTE` and
  receives the shared blend.
- When all items in a group share `blend`, item-level copies into the group buffer
  should render at full strength and the final sprite should carry the shared
  blend. For the loader/window shadow this means black shadow pixels are stored
  opaque in the buffer, then the whole sprite blends at 30 percent.
- Script-created indexed window buffers start as opaque white with palette indices
  initialized. The final `MATTE` pass must remove that white backing so only the
  authored shadow or chrome pixels remain. If the white backing survives and the
  sprite has partial blend, it appears as a grey halo on black stage backgrounds.
- Window shadow artifacts are usually a matte/index problem before they are a
  z-order problem. Verify the baked shadow bitmap first: black shadow pixels should
  remain opaque, and edge-connected white backing should become fully transparent.

### Masks

- `createMatte()` and `createMask()` are related but not interchangeable.
  `createMatte()` creates alpha-style matte data from native alpha when present,
  otherwise from flood-filled background. `createMask()` can create direct mask
  images for mask-source content; for ordinary images it falls back to flood-filled
  matte behavior.
- `maskImage` uses luma-style mask semantics: white blocks drawing and black allows
  drawing. In code, `Drawing.maskAllowsPixel(mask, x, y)` is true when
  `maskAlphaFromPixel(pixel) < 255`.
- A score sprite using Director `MASK` ink uses the immediately following cast
  bitmap as its mask. Apply that mask to the source alpha (black reveals, white
  hides), then apply the sprite blend once; do not derive opacity from the source
  artwork color itself.
- Explicit `#maskImage` properties must remain honored. Native-alpha source images
  do not automatically invalidate authored masks.
- When scaling with a mask, evaluate the mask at original source coordinates.

### Native Alpha

- A 32-bit member is opaque unless the bitmap carries native-alpha metadata.
- Native-alpha bitmaps normally use alpha for transparency, but an opaque border
  key can still be keyed by `BACKGROUND_TRANSPARENT`.
- Non-native alpha zeroes from container data should be exposed as opaque before
  script-level operations that expect non-alpha 32-bit image behavior.

### Text, Remap, and Color

- `#color` and `#bgColor` remap should apply only to mostly grayscale sources.
  Already-colored images or text should keep their authored RGB values.
- White-backed grayscale text copied into a compatible destination may treat white
  as transparent.
- Near-white text glyphs and overlays must not be stripped by matte heuristics.
- In the C++ `SpriteBaker`, live runtime images for text members take precedence
  over text re-rendering. If a field/text member has a runtime bitmap supplied by
  the live bitmap provider, bake that image through the normal live-bitmap
  processing path instead of invoking `TextRenderer`.

### Dynamic Runtime Bitmaps

- A dynamic runtime bitmap can be authoritative even when `Bitmap::isScriptModified()`
  is false. Runtime-created members and script-fed UI buffers may carry valid pixel
  content without that flag.
- In `SpriteBaker::bakeBitmap`, use the live bitmap provider for dynamic members
  when the live bitmap has meaningful dimensions, not only when `isScriptModified()`
  is true. This is required for script-built UI images such as the Navigator Public
  Spaces illustration.
- Preserve the existing `processLiveBitmap` path for runtime images so
  background-transparent and matte handling remains centralized.
- If preserving opaque script-built runtime backing pixels, use runtime provenance
  from the Lingo image operations, the current window-wrapper path, the `copyPixels`
  source/dest geometry, and source content. Verify the target UI window and
  adjacent always-visible surfaces such as the Navigator, catalogue pages, or
  loaded room playfield.
- Do not add special cases for individual member names or room names. If a runtime
  bitmap is present but not rendering, fix the generic dynamic-member bake path.
- If a runtime bitmap bakes as fully transparent, inspect the source runtime bitmap
  first. An all-white source after script composition means the producer path
  failed; changing final matte handling would only hide the real bug.

### Darken, Lighten, and Color Ramps

- For `DARKEN` and `LIGHTEN`, opaque white 32-bit non-alpha buffers can be neutral
  content, not automatic matte.
- Dynamic indexed sprites that depend on color ramps require preserved palette
  indices. Later text, timestamp, or wrapper copies must not clear base image
  indices.
- Indexed darken color-ramp exactness still needs care when future work touches
  palette-index copy or refresh paths.

### Rendering Verification

- Prefer focused tests that recreate script image operations and assert both
  visible pixels and palette indices where relevant.
- For `SpriteBaker` changes, add tests for authored bitmap members, dynamic bitmap
  members, authored text members, and dynamic text members when the change affects
  live bitmap selection.
- When changing white-keying or runtime-backing preservation, include regression
  cases for a marked image-wrapper sparse viewport composite that should remain
  opaque, and full-size artwork slots, Navigator text/link mattes, product/list
  strips, or stage-sized runtime surfaces that should still key white transparent.
- For window-buffer fixes, verify at least these surfaces together: Navigator
  Public Spaces/Rooms text areas, Messenger/Friends after opening the toolbar
  icon, Catalogue Collectables product strip, and the private room
  playfield/furniture load. A preservation rule that fixes only one window can
  easily reintroduce broad white fills elsewhere.
- Use pixel counts around the target region and at least one adjacent control
  region. A fix that restores one window can still be wrong if it introduces large
  white fills elsewhere in the same frame.
- Include a non-`scriptModified` dynamic runtime bitmap test. The expected behavior
  is that a meaningful dynamic runtime image still bakes.
- Run narrow tests for the affected behavior first, then broader bitmap or VM
  suites when the change touches shared copy or image storage code.

## Desktop Debugger and Recording Replay

The Qt desktop debugger (`debugger/cpp/`) offers movie playback, bytecode and
decompiled code views, breakpoints, stepping, call-stack/variable/watch inspection,
and input recording with replay.

Build and run:

```bash
./build.sh --release --target libreshockwave_debugger --no-tests
./cmake-build-release/cpp/libreshockwave_debugger_app/libreshockwave_debugger [movie-or-recording]
```

Always test the desktop debugger in Release mode, never Debug mode. Pass a movie
or recording as the positional argument; `--play` starts ordinary playback or
recording replay after loading (including asynchronous HTTP(S) movies):

```bash
libreshockwave_debugger --play path/to/movie.dcr
libreshockwave_debugger --play path/to/session.lswdebug
```

Open local `.dir`, `.dcr`, `.dxr`, `.cct`, `.cst` files via **File → Open Movie**,
HTTP(S) movie URLs via **File → Open URL**, and set `key=value` external parameters
via **Parameters → Edit Parameters** (reload the movie after changing them). The
window remembers recent movies, parameters, layout, and breakpoints per movie.

### Replaying Recordings

- **Play & Record** saves stage mouse and keyboard input as a `.lswdebug` file.
  Input is flushed continuously (about once per second), so a crash mid-session
  still leaves a usable recording.
- Replay a recording with **File → Open Debug Recording** (or open the `.lswdebug`
  from **File → Open Movie**, or pass it on the command line with `--play`). The
  recording embeds the movie path/URL and the external parameters in its header and
  reloads them, so a replay reproduces the same movie and connection parameters.
- Recordings are plain newline-delimited JSON (version 2): the first line is the
  header (`format`, `version`, `movie`, `externalParams`), then one JSON object per
  event with `timeMs`, `type` (`mouseMove`/`mouseDown`/`mouseUp`/`keyDown`/`keyUp`),
  `stageX`, `stageY`, `keyCode`, `keyText`, `shift`, `ctrl`, `alt`, `rightButton`.
  An incomplete trailing line is ignored. Because recordings are plain JSON, you
  can hand-edit them — for example, retarget a click or reorder events — to build
  focused diagnostics.
- Mouse-move events are throttled (a sample at least every 50 ms when the position
  changes); clicks and keys are recorded losslessly.
- Replay timing is relative to usable movie time, not wall-clock time: the replay
  clock only advances while the player is `networkReady()` and the movie is loaded,
  so events are never injected into an empty or loading score. The clock also
  pauses while a recording-triggered movie navigation fetches the next movie, then
  resumes when the new session is ready.
- Replays run against the live network. If the server data changed since recording,
  the recorded click path can diverge (different room list, different navigation
  result, different handshake). Before assuming a renderer regression, check the
  dated notes in `docs/goals/` for past replay-divergence investigations.
- For visual verification of a replay, set `LIBRESHOCKWAVE_DEBUGGER_FRAME_DUMP` to a
  PNG path (rolling frame snapshot, at most once per second) and optionally
  `LIBRESHOCKWAVE_DEBUGGER_FRAME_STATS` (per-second presentation FPS) before
  launching the debugger.

Playback and stepping shortcuts:

| Key | Action |
|-----|--------|
| `F5` | Continue |
| `Esc` | Pause |
| `F9` | Toggle breakpoint (at current instruction while paused, else at last clicked gutter line) |
| `F10` | Step over |
| `F11` | Step into |
| `Shift+F11` | Step out |

## Network Architecture: WebSocket (WASM) vs Raw TCP (native)

The game connection (Multiuser Xtra) is transport-agnostic behind the
`MultiuserNetBridge` interface, but the two builds use different transports. Know
which one you are testing:

- **Browser/WASM**: the C++ side registers a `QueuedMultiuserBridge`; the host
  worker (`web/libreshockwave-cpp-worker.js`) polls connect/send requests
  (`lsw_poll_multiuser_requests`) and opens a **WebSocket** per connection —
  `new WebSocket(ws[s]://host:port[/websocketPath])`, using `wss` when the page is
  HTTPS or `websocketSsl` is set, with binary frames and sends queued while
  CONNECTING. Incoming bytes are delivered back through `lsw_multiuser_message_bytes`
  and friends. The browser cannot open raw TCP sockets, so the WebSocket relay is
  the only transport there.
- **Native builds and the desktop debugger**: the plain `Player` registers
  `SocketMultiuserBridge`, which opens **raw TCP sockets** directly to the
  configured hosts — `getaddrinfo` with `SOCK_STREAM`, then `socket`/`connect`/
  `send`/`recv` on POSIX or Winsock, with no WebSocket framing and no HTTP upgrade.
  The debugger connects straight to `connection.info.host:port` and the MUS port
  from the external parameters.

Implications:

- Verify networking behavior in the transport your change targets. A fix proven
  over WebSocket may not exercise the same framing or pacing code as raw TCP, and
  vice versa.
- A recording replayed in the desktop debugger uses raw TCP against the live server;
  the same movie in the browser uses the WebSocket relay. Reachability, packet
  pacing, and handshake behavior differ between the two.
- When diagnosing connection issues, state which transport was used (WebSocket
  worker vs native TCP socket bridge).

## Verification Practice

- Add focused regression tests that model the Director behavior in isolation. Do
  not encode asset names, member names, room names, or script-specific workarounds
  into tests.
- For white-keying fixes, final visual acceptance covers all four canaries from
  the same rebuilt WASM bundle: Navigator, Messenger/Friends, Catalogue
  Collectables, and a loaded private room. For private-room regressions, use
  Firefox/geckodriver and repeat the load because furniture timing can vary.
- For toolbar/icon repros, prefer a full mouse move/down/up/click sequence at the
  stage coordinate. A synthetic click-only event can fail to trigger
  Director-style pressed/released handlers.
- Record white-pixel counts for the affected region and at least one adjacent
  control region when investigating white-keying regressions; keep the expected
  image and actual canvas paths in the investigation note.
- Capture screenshots or state JSON for visual/browser verification when the
  acceptance criteria are visual or interactive.
- Known generic fix areas that must stay generic, preserving invalidation
  boundaries and Director semantics (caches must not hide dynamic member creation,
  alias index imports, later top-level callbacks, or script-instance mutation):
  VM fast prop-list object-call behavior for nested list/property access; XML
  Xtra/property traversal and Director-compatible collection semantics;
  script-instance property indexing and exact-name lookup; short-lived
  script-handler lookup caches scoped to a top-level handler; `script()` lookup
  caching by cast-library scope and normalized script name; member-registry alias
  refresh and fallback member-name resolution caching; generic list and prop-list
  immediate object-call handling; `StringBuiltins::offset` avoiding avoidable
  string copies; input hit selection preferring topmost interactive bounding-box
  hits where authored window element sprites need the event.
