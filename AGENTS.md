# AGENTS.md: LibreShockwave Working Guidelines

This file is the working guide for this repository. It covers how to approach
bugs, where fixes must live, how to verify them, and how to use the desktop
debugger, its recordings, and both network transports. Read it before starting
an investigation. Keep it current after meaningful changes.

LibreShockwave is a C++ reimplementation of the Shockwave and Director runtime.
It plays Director movies (`.dir`, `.dcr`, `.dxr`, `.cct`, `.cst`). It runs the
Lingo virtual machine, the renderer, the cast and member system, and the
multiuser (MUS) network stack. It runs in two builds: native (desktop debugger,
raw TCP sockets) and WASM (browser, WebSocket relay).

## Engineering Principles

Write for a capable developer who never saw this code. They must be able to
read, fix, and extend it without asking.

- Prefer clarity over cleverness. Explicit beats implicit. Minimize magic. If a
  trick needs explaining, write the plain version.
- Complexity budget: small functions, cohesive modules. No hidden globals. State
  flows through the `Player`, the debugger contexts, and the provider and bridge
  objects. It uses constructor injection and explicit setter wiring. There is no
  process-wide singleton. Keep it that way. External parameters travel
  explicitly. They use `Player::setExternalParams`, the debugger parameters
  dialog, and recording headers. Never globals. Shared mutable state is allowed
  only where the architecture already demands it. That is the cross-thread
  queues between the VM thread and the debugger UI.
- The pillars this project is judged on:
  1. Fidelity: the runtime reproduces Director and Shockwave semantics
     generically. App-authored Lingo stays app-authored Lingo. No C++ branches
     keyed to asset, script, room, or user names.
  2. Performance: VM dispatch, handler dispatch, image and copy paths, and
     networking are fast. Hot paths avoid needless copies, allocations, and
     abstraction overhead.
  3. Portability: one core runs on native (desktop debugger, raw TCP) and WASM
     (browser, WebSocket relay). It sits behind the `MultiuserNetBridge` and
     the transport-agnostic interfaces.

## Architecture Rules

### Transport abstraction
- The game connection (Multiuser Xtra) is transport-agnostic behind the
  `MultiuserNetBridge` interface.
- The native build registers `SocketMultiuserBridge`. It opens raw TCP sockets
  directly to the configured hosts. It has no WebSocket framing and no HTTP
  upgrade.
- The WASM build registers `QueuedMultiuserBridge`. The host worker
  (`web/libreshockwave-cpp-worker.js`) polls connect and send requests. It opens
  one WebSocket per connection. The browser cannot open raw TCP sockets. The
  WebSocket relay is the only transport there.
- One transport per build. Verify behavior in the transport the change targets.
  A fix proven over WebSocket may not exercise the same framing or pacing code
  as raw TCP. And vice versa. State which transport was used.
- The detailed framing and pacing behavior lives in the Network Architecture
  section.

### Lingo is data
- Treat app-authored Lingo as app-authored Lingo. The runtime must not grow C++
  branches keyed to parent-script names, movie-script names, handler names,
  Resource API helper names, or room and login script names.
- A new movie is a new set of cast members and scripts. It runs with zero C++
  edits.
- Before treating a helper as a missing builtin, search the exported Lingo and
  asset text first. Some helpers that look like builtins are authored
  movie-script APIs. `memberExists` is one. It is defined in
  `MovieScript 9 - Resource API.ls` under `v31_assets`. It delegates to the
  Resource Manager.

### Layer ownership
- No page-specific, room-specific, member-name-specific, or user-name-specific
  fixes. Keep fixes in the layer that owns the failing behavior. That layer is
  the C++ runtime, VM, Xtra, networking, cast and member, input, or renderer.
- Performance improvements must be generic VM and runtime work. They cover
  Director semantics, bytecode dispatch, handler dispatch, list and property-list
  behavior, script-instance properties, arithmetic, strings, builtins, data
  structures, cooperative scheduling, and cache behavior.
- The extensibility test: a new movie runs with zero C++ edits. A fix that
  belongs to one movie stays in the assets. A fix that belongs to shared
  behavior lives in the C++ runtime, VM, Xtra, renderer, or networking layer.

## Hard Rules

These are hard rules. The project owner set them.

1. **C++ naming convention**
   - Types (class, struct, enum, alias, concept): `PascalCase`
   - Functions, methods, variables, members: `snake_case`
   - Private members: trailing underscore (`read_buffer_`)
   - Namespaces: `snake_case` (`libreshockwave::player::net`)
   - Files: `PascalCase.hpp` / `PascalCase.cpp`. Match the existing tree
     (`NetManager.hpp`). Test files use `snake_case_test.cpp`
     (`sdk_foundation_test.cpp`)
   - Macros: `SCREAMING_SNAKE_CASE`. Avoid macros where possible.
2. **No duplicate functions**
   Every piece of logic has exactly one canonical implementation. Shared logic is
   extracted into a single free function. The appropriate module owns it. Never
   copy-paste-adapt.
3. **No trivial pass-through methods**
   Do not write a method whose body only calls one other method and returns its
   result. Call the target directly. Only allow a wrapper when it adds a
   contract. The contract is synchronization, caching, a stable public API over
   changing internals, or virtual dispatch.
4. **One responsibility per file, under 600 lines**
   One primary class or component per header and source pair. Every file stays
   under 600 lines. This is a hard limit. When a file approaches it, split
   responsibilities. Do not append. Keep only a few files per folder. Roughly
   five or fewer. When a module outgrows that, split it into subfolders by
   concern. Mirror the layout under `cpp/include/libreshockwave/`, `cpp/src/`,
   and `cpp/tests/`. Existing examples are `player/net/`, `player/xtra/`, and
   `lingo/vm/datum/`.
5. **Modern C++**
   RAII. Const-correctness. `[[nodiscard]]` on anything returning an error,
   status, or a new object. `std::span` instead of raw pointer and length
   pairs. Include what you use. Forward declarations in headers. `#pragma once`.
6. **Error handling**
   Exceptions are only for programmer errors and fatal startup failures. Runtime
   I/O, parsing, and network errors use `std::error_code` or `std::optional` or
   result-style returns. Follow the style of the module being touched.
   Exceptions never cross thread boundaries. That is the VM thread to the
   debugger UI.
7. **Formatting**
   4 spaces. Braces open on the same line as the statement. They close on their
   own line. 100-column limit. `const` before the type (`const int`). `auto`
   only where it removes noise (iterators, `make_unique`). One declaration per
   line.
8. **No band-aid fixes**
   Root-cause fixes only. Never ship shims, workarounds, or half-assed patches
   that paper over a symptom. If the correct fix turns out larger than expected,
   do it properly. Split files as rule 4 requires. If unsure of the right
   approach, ask Alex. Do not commit a hack.
9. **No new tests**
   Do not make new tests. Do not add test files, test functions, or test
   targets. The runtime coverage lives in `cpp/tests/sdk_foundation_test.cpp`.
   The desktop-debugger coverage lives in `debugger/cpp/debugger_test.cpp`. Do
   not grow them. New suites fragment `ctest` output and bloat configure time.
10. **No unsolicited docs files**
   Do not create new files under `docs/`. Do not create new `.md` files
   anywhere. Only when the user explicitly asks or an approved plan calls for
   them.
11. **Comments are max 4 lines**
   A comment may span at most 4 consecutive lines. This covers a single `//`
   line and a `/* */` block. Prefer fewer short lines. Say what is non-obvious.
   Never restate the code. Public-API doc comments in headers (rule 12) are
   exempt from the 4-line cap. Inline comments inside function bodies are not.
12. **Docstrings on every public API**
   Every public type and function in `cpp/include/libreshockwave/` carries a
   brief `//` block comment directly above it. It states what it does, its
   preconditions and contracts, and a short usage example for non-obvious APIs.
   Non-obvious APIs are parsers, result-style returns, and live-bitmap wiring.
   Match the surrounding file comment style. Do not introduce doxygen-style
   markup. Tricky private logic gets an inline comment at the definition. Keep
   it within the rule 11 cap.
13. **No magic numbers**
   Every literal falls in exactly one bucket.
   - Director and Shockwave format constants. Chunk tags, version semantics,
     score and frame rules, SMUS framing. Named constants, never configurable.
     Never move these to external parameters.
   - Tunables. Timeouts, buffer sizes, throttle caps, frame budgets. Named
     file-local constants. A value that must vary per deployment comes through
     the external parameters. Never an inline literal.
   - Asset-owned values. Lingo-visible rules and limits. A file-local named
     constant plus a one-line source comment. Mark it as asset-owned or
     deferred.
   Style: file-local `constexpr` with `kCamelCase` names. Examples:
   `kRecordingFlushIntervalMs`, `k_listener_tag`.
14. **Logs are actionable**
   Every diagnostic line carries identifying context in a message prefix.
   Examples: `connection 5: ...`, `event loop 1: ...`. It states what happened
   and what it means. The line alone suggests the next action. Handle every
   error. Never swallow silently. Log each one once, at the right level, where
   it is handled. Use the existing diagnostic channels. They are the debugger
   output handlers, the status bar, and the WASM debug-message poll. Do not
   invent a new logging framework.
15. **Security**
   Validate all external input at the boundary. Cover Director chunk and frame
   lengths, multiuser and SMUS frame sizes, and network results. Parsers reject
   malformed input. They do not push the check onto callers. Secrets and
   credentials come only from external parameters or the environment. Never in
   code, tests, or the repo. The connection presets in the debugger parameters
   dialog are local defaults. They are never committed credentials.
16. **Deterministic where feasible**
   Tests use fixed RNG seeds. They never depend on wall-clock time. Inject or
   parameterize time instead. The debugger replay clock does this. Never iterate
   a `std::unordered_map` and depend on its order.
17. **Documentation style**
   Write markdown in plain standard English. Use the style of ASD-STE100
   Simplified Technical English. Short sentences. One idea per sentence. No
   idioms. No contractions. Do not use em dashes. Use a period, a comma, or a
   new sentence instead.

## Working Principles

These are repository-specific constraints. They sit on top of the hard rules.

- Canonical examples: `BigInt`, `HugeInt`, `HugeInt15`. Do not implement native
  C++ branches for `BigInt`, `HugeInt`, `HugeInt15`, `powMod`, `Modulo`, `div`,
  `getString`, or `getByteArray`. The same rule applies to hot asset handlers.
  They are `getmemnum`, `memberExists`, `updateProcess`,
  `createPassiveObject`, `solveMembers`, and `parseCallback`.
- Do not restore C++ hardcoding for `pProcList`, `registerProcedure`, or
  `removeProcedure`.
- Do not seed synthetic broker instances with asset-owned state. Asset-owned
  state means proc-list templates and fixed event maps. If broker support is
  needed, preserve and dispatch real authored broker script instances and their
  properties.
- Search with `rg` before assuming a behavior is missing. Read the relevant
  dated file in `docs/goals/` before drafting a new investigation.
- When an issue reproduces only in a specific movie, inspect the extracted files
  before editing C++. Inspect the `.ls`, `.lasm`, `.window`, `.props`,
  `manifest.tsv`, and bitmap and text exports.
- If a diagnostic hardcoded path is used briefly to prove a cause, remove it
  before finalizing. The permanent fix must obey the generic-runtime rules
  above.

## Git

- Commit messages: subject and body as the work warrants.
- No `Co-Authored-By:` or any machine-attribution trailers.
- Commit messages must not mention game, asset, room, user, or endpoint names.
  Describe the generic runtime behavior being fixed.

## Building and Testing

Relevant build targets:

```bash
cmake --build cmake-build-debug --target libreshockwave_tests -j2
cmake --build cmake-build-wasm --target libreshockwave_cpp_wasm_dist -j2
```

Run the test suite with `ctest --test-dir cmake-build-debug --output-on-failure`.

- The runtime coverage lives in `cpp/tests/sdk_foundation_test.cpp`. The
  desktop-debugger coverage lives in `debugger/cpp/debugger_test.cpp`. Do not
  add new test files, functions, or targets (rule 9).
- The full native test binary has a known pre-existing abort. It is around a
  `sdk_foundation_test.cpp` `gcCallbacks == 1` assertion. If it still appears,
  document the exact assertion. Run the focused tests that cover the current
  change.
- When serving the WASM distribution (usually from
  `/opt/git/LibreShockwave/cmake-build-wasm/cpp/wasm-dist`), restart the static
  server after rebuilding. This stops deleted old bundle file descriptors from
  confusing the run.

## Where Things Live

- `cpp/include/libreshockwave/` holds the public C++ API.
- `cpp/src/` holds the runtime, VM, Xtra, renderer, and networking.
- `cpp/src/player/` holds the `Player`, score and frame, sprite, cast, input,
  media, and audio. The `net/`, `render/`, and `xtra/` subfolders split those
  concerns.
- `cpp/src/lingo/` holds the Lingo VM, builtins, and decompiler. The `vm/` and
  `xtra/` subfolders split those concerns.
- `cpp/src/lingo/vm/` holds `LingoVM`, dispatch, parse, and trace. The `datum/`
  subfolder holds datum types.
- `cpp/src/lingo/xtra/` holds `MultiuserXtra`, `XmlParserXtra`, and
  `XtraManager`.
- `cpp/src/bitmap/` holds `Bitmap`, `Drawing`, `Palette`, and the image
  decoders.
- `cpp/tests/sdk_foundation_test.cpp` holds the runtime test suite.
- `debugger/cpp/` holds the Qt desktop debugger. `debugger_test.cpp` holds its
  test suite.
- `web/` holds the WASM host worker (`libreshockwave-cpp-worker.js`) and the
  player glue.
- `docs/goals/` holds dated investigation notes.

## Repository and Reference Paths

Primary repository: `/opt/git/LibreShockwave`

Nearby asset and reference repositories:

- `/opt/git/v31_assets` (mirror: https://github.com/Quackster/v31_assets/). It
  is the main local source for v31 cast exports and Lingo dumps. Layout:
  `bitmaps/` (decoded PNG members), `text/` (STXT/XMED payloads, including
  `.window`, `.props`, `.index`, and config-like text members), `sounds/`,
  `palettes/` (TSV files), `raw_chunks/` (member-owned raw chunks),
  `manifest.tsv`, `file_info.tsv`, and
  `projectorrays_lingo/<file>/casts/**/*.ls` (ProjectorRays Lingo dumps).
- Frequently relevant paths under `v31_assets`:
  `/opt/git/v31_assets/hh_interface.cct`,
  `/opt/git/v31_assets/projectorrays_lingo/fuse_client`, and the `HugeInt15`,
  `Login Handler Class`, `Connection Instance Class`, `Multiuser Instance Class`,
  and `Event Broker Behavior` scripts under
  `projectorrays_lingo/*/casts/External/`.
- `/opt/git/v14_assets` and `/opt/git/v1_assets`. They have the same extraction
  shape as `v31_assets`. They are useful for comparing older behavior and
  Director extraction output.
- `/opt/git/v1_assets/handshake-demo`. It is a minimal Node.js TCP demo server
  for the v1 EnterpriseServer-style handshake. It only gets the client out of
  the connection-wait loop. It does not implement login, rooms, messenger
  state, or gameplay.
- `/opt/git/test/LibreShockwave`. It holds older investigation notes (e.g.
  private-room wall rendering). Key lesson: trace sprite references and authored
  initialization paths before changing broad rendering behavior.

Native visual references (evidence and comparison targets only). Do not encode
their scene or member names into generic runtime behavior:

- `/home/alex/Pictures/Screenshots/Screenshot_20260615_215344.png`

Authored call paths that are useful for diagnosis but must stay authored Lingo:

- Bottom-left head icon: `updateEntryBar` → `createMyHeadIcon` →
  `Figure_Preview.createHumanPartPreview` → `getHumanPartImg(#head, ...)` →
  `Human Class EX.getPartialPicture(#head, ...)`.
- Welcome Lounge networking and performance:
  `Connection Instance Class.xtraMsgHandler`,
  `Login Handler Class.responseWithPublicKey`,
  `Login Handler Class.handleServerSecretKey`, `HugeInt15.powMod`,
  `HugeInt15.Modulo`, `HugeInt15.div`, `HugeInt15.getString`,
  `HugeInt15.getByteArray`.

Fix the generic runtime behavior underneath these paths.

## Rendering Rules

Validated C++ renderer behavior. Treat these as rules for future fixes and
tests. They are not a complete rendering specification.

### General Approach

- Prefer root-cause fixes over post-render pixel repair. Fix image storage, VM
  image methods, ink handling, and copy paths. Keep fixes in the C++ runtime,
  renderer, VM, and cast and member layers. Do not compensate with client-side
  shims or hardcoded Lingo implementations.
- Keep indexed-image metadata and rendered ARGB pixels consistent. If a path
  changes visible pixels on an indexed image, it may also need to update
  `paletteIndices`.
- Preserve authored transparency and masks. Avoid broad heuristics that turn
  ordinary light pixels into transparent pixels.
- When changing one rendering path, rerun focused tests for adjacent behavior.
  Cover palette copies, matte and background-transparent ink, masks, quad
  transforms, and text and color remap.
- If a visual regression appears in a script-created UI surface, inspect the
  baked `RenderSprite`. Inspect its dynamic `CastMember` and the member runtime
  bitmap. Do this before changing matte heuristics. A missing sprite can be a
  bake-source selection problem. It is not a transparency problem.

### Image Creation and Palette State

- `image(width, height, depth[, palette])` starts as opaque white.
- For indexed images, attach the palette before the initial white fill. This
  initializes the backing pixels and palette indices together.
- `fill(..., paletteIndex(n))` on indexed images must update both the ARGB pixel
  data and `paletteIndices`.
- Integer palette-index fills on images with depth `<= 8` are indexed fills.
- RGB fills on 32-bit images that carry a palette reference resolve RGB
  normally. They do not create an indexed image.

### Palette Index Preservation

- Preserve source palette indices when copying from an indexed source into a
  compatible indexed destination. This applies to full-blend `COPY`, `MATTE`,
  and `BACKGROUND_TRANSPARENT` operations with no mask or remap.
- For `BACKGROUND_TRANSPARENT`, transparent source pixels must not overwrite the
  destination pixel or its destination palette index.
- When copying RGB or non-indexed pixels into an indexed destination that
  already has palette indices, refresh indices only inside the copied
  rectangle. Use the destination palette. Do not clear the entire index plane
  because part of an indexed destination received RGB pixels.
- When copied RGB white lands on an indexed destination, keep an existing index
  `0` only when the destination already had index `0` and palette entry `0` is
  white. Otherwise choose the nearest non-matte palette index. This stops
  ordinary white text or panel pixels from becoming holes in later matte
  operations.
- Scaled copies must scale palette indices with pixels when indices are
  preserved.

### Quad Copying

- Quad destinations use this point order: top-left, top-right, bottom-right,
  bottom-left.
- Axis-aligned quads cover identity, flips, and 90-degree transforms.
- Preserve transformed source palette metadata and indices when available.
- The fast quad path may handle plain `ink` and `blend`. Prop lists containing
  masks, darken or lighten behavior, remaps, or other copy properties should fall
  back through rectangular `copyPixels`. This keeps the existing property
  semantics active.
- Do not route all quads through the rectangular path. Landscape and mask
  transforms depend on the quad path keeping its transform behavior.
- Keep landscape and mask regression coverage when touching quad-copy, mask,
  matte, or background-transparent paths.

### Matte and Background-Transparent Ink

- `MATTE` ink uses a flood-fill matte from border-connected background pixels.
- If valid palette indices exist, prefer palette-index matte detection. Border
  index `0` white is normal matte and background when present and the image is
  not uniform.
- For script-built indexed UI buffers, sprite-level `backColor` is still
  meaningful for final `MATTE`. If it resolves to white through the active
  palette or as packed RGB, prefer the matching indexed white matte before the
  dominant-edge heuristic. This prevents dark shadow art on the edges from being
  mistaken for the matte color while the original white backing remains visible.
- Without valid indices, use the RGB flood-fill fallback. It is usually keyed
  from white.
- `BACKGROUND_TRANSPARENT` ink defaults the background key to white unless
  `bgColor` provides a different key.
- `BACKGROUND_TRANSPARENT` copies from indexed sources with the default white key
  should skip the authored matte index when palette index `0` is near-white.
  They must not erase duplicate white or near-white RGB pixels that came from
  other palette entries.
- Background-transparent copies should treat border-connected key pixels as
  transparent. They preserve enclosed key-colored content when required.
- Script-built runtime backings under `BACKGROUND_TRANSPARENT` need provenance.
  Use the Lingo window framework plus copy geometry and source content. Do not
  treat every white `image.fill()` as backing that should survive final baking.
  In the fuse_client framework, `Image Wrapper Class.feedImage()` is the path
  that fills `pBuffer.image` and then renders a source image into it. Only that
  transaction may mark a possible preserved fill backing.
- Not every `feedImage` result should preserve white. Navigator text and link
  surfaces are also `#type: "image"` wrappers fed with writer-rendered images.
  Their white is a matte and must still key out. Preserve the wrapper fill only
  when the source is a script-created rectangular image copied with
  `BACKGROUND_TRANSPARENT`. The requested source rectangle must extend beyond the
  source image. The visible source must look like sparse viewport or list
  content. It must not be a text or link matte or an artwork slot. Full-size
  catalogue artwork, product strips, Navigator text and link mattes, untouched
  `image()` default white, generated text mattes, and room and compositor
  surfaces must stay on the normal white-keying path.
- Near-white matte preprocessing must stay narrow. Apply it only when the source
  has a border-connected near-white matte and real non-near-white content. A
  broad near-white rule can erase legitimate light text, highlights, and
  overlays.
- Outlined-white-body preservation is not a general script-buffer rule. Keep it
  for authored low-depth assets and explicitly scoped script-built 32-bit chat
  backgrounds. Do not apply it to script-modified indexed window buffers. There,
  white is usually the matte backing.

### Window Buffers and Grouped UI

- Window layouts group repeated element IDs into one bitmap buffer per group. In
  `habbo_simple.window`, `shadow` is built before `back`. The shadow group uses
  mixed item inks with a shared blend. So the final sprite stays `MATTE` and
  receives the shared blend.
- When all items in a group share `blend`, item-level copies into the group
  buffer should render at full strength. The final sprite should carry the
  shared blend. For the loader and window shadow, this means black shadow
  pixels are stored opaque in the buffer. Then the whole sprite blends at 30
  percent.
- Script-created indexed window buffers start as opaque white with palette
  indices initialized. The final `MATTE` pass must remove that white backing.
  Only the authored shadow or chrome pixels remain. If the white backing
  survives and the sprite has partial blend, it appears as a grey halo on black
  stage backgrounds.
- Window shadow artifacts are usually a matte or index problem before they are a
  z-order problem. Verify the baked shadow bitmap first. Black shadow pixels
  should remain opaque. Edge-connected white backing should become fully
  transparent.

### Masks

- `createMatte()` and `createMask()` are related but not interchangeable.
  `createMatte()` creates alpha-style matte data from native alpha when present.
  Otherwise it uses flood-filled background. `createMask()` can create direct
  mask images for mask-source content. For ordinary images it falls back to
  flood-filled matte behavior.
- `maskImage` uses luma-style mask semantics. White blocks drawing. Black allows
  drawing. In code, `Drawing.maskAllowsPixel(mask, x, y)` is true when
  `maskAlphaFromPixel(pixel) < 255`.
- A score sprite using Director `MASK` ink uses the immediately following cast
  bitmap as its mask. Apply that mask to the source alpha (black reveals, white
  hides). Then apply the sprite blend once. Do not derive opacity from the
  source artwork color itself.
- Explicit `#maskImage` properties must remain honored. Native-alpha source
  images do not automatically invalidate authored masks.
- When scaling with a mask, evaluate the mask at original source coordinates.

### Native Alpha

- A 32-bit member is opaque unless the bitmap carries native-alpha metadata.
- Native-alpha bitmaps normally use alpha for transparency. An opaque border key
  can still be keyed by `BACKGROUND_TRANSPARENT`.
- Non-native alpha zeroes from container data should be exposed as opaque before
  script-level operations. Those operations expect non-alpha 32-bit image
  behavior.

### Text, Remap, and Color

- `#color` and `#bgColor` remap should apply only to mostly grayscale sources.
  Already-colored images or text should keep their authored RGB values.
- White-backed grayscale text copied into a compatible destination may treat
  white as transparent.
- Near-white text glyphs and overlays must not be stripped by matte heuristics.
- In the C++ `SpriteBaker`, live runtime images for text members take precedence
  over text re-rendering. If a field or text member has a runtime bitmap
  supplied by the live bitmap provider, bake that image through the normal
  live-bitmap processing path. Do not invoke `TextRenderer`.

### Dynamic Runtime Bitmaps

- A dynamic runtime bitmap can be authoritative even when
  `Bitmap::isScriptModified()` is false. Runtime-created members and script-fed
  UI buffers may carry valid pixel content without that flag.
- In `SpriteBaker::bakeBitmap`, use the live bitmap provider for dynamic members
  when the live bitmap has meaningful dimensions. Do not require
  `isScriptModified()` to be true. This is required for script-built UI images
  such as the Navigator Public Spaces illustration.
- Preserve the existing `processLiveBitmap` path for runtime images. This keeps
  background-transparent and matte handling centralized.
- If preserving opaque script-built runtime backing pixels, use runtime
  provenance. Use the Lingo image operations, the current window-wrapper path,
  the `copyPixels` source and destination geometry, and source content. Verify
  the target UI window and adjacent always-visible surfaces. Those are the
  Navigator, catalogue pages, or loaded room playfield.
- Do not add special cases for individual member names or room names. If a
  runtime bitmap is present but not rendering, fix the generic dynamic-member
  bake path.
- If a runtime bitmap bakes as fully transparent, inspect the source runtime
  bitmap first. An all-white source after script composition means the producer
  path failed. Changing final matte handling would only hide the real bug.

### Darken, Lighten, and Color Ramps

- For `DARKEN` and `LIGHTEN`, opaque white 32-bit non-alpha buffers can be
  neutral content. They are not automatic matte.
- Dynamic indexed sprites that depend on color ramps require preserved palette
  indices. Later text, timestamp, or wrapper copies must not clear base image
  indices.
- Indexed darken color-ramp exactness still needs care when future work touches
  palette-index copy or refresh paths.

### Rendering Verification

- Verify with the existing tests that recreate script image operations. Assert
  both visible pixels and palette indices where relevant. Do not add new tests
  (rule 9).
- For `SpriteBaker` changes, check the existing coverage for authored bitmap
  members, dynamic bitmap members, authored text members, and dynamic text
  members when the change affects live bitmap selection.
- When changing white-keying or runtime-backing preservation, check the existing
  regression cases. They cover a marked image-wrapper sparse viewport composite
  that should remain opaque. They also cover full-size artwork slots, Navigator
  text and link mattes, product and list strips, or stage-sized runtime
  surfaces that should still key white transparent.
- For window-buffer fixes, verify at least these surfaces together: Navigator
  Public Spaces and Rooms text areas, Messenger and Friends after opening the
  toolbar icon, Catalogue Collectables product strip, and the private room
  playfield and furniture load. A preservation rule that fixes only one window
  can easily reintroduce broad white fills elsewhere.
- Use pixel counts around the target region and at least one adjacent control
  region. A fix that restores one window can still be wrong if it introduces
  large white fills elsewhere in the same frame.
- Check the existing non-`scriptModified` dynamic runtime bitmap case. The
  expected behavior is that a meaningful dynamic runtime image still bakes.
- Run the narrow tests for the affected behavior first. Then run broader bitmap
  or VM suites when the change touches shared copy or image storage code.

## Desktop Debugger and Recording Replay

The Qt desktop debugger (`debugger/cpp/`) offers movie playback, bytecode and
decompiled code views, breakpoints, stepping, call-stack and variable and watch
inspection, and input recording with replay.

Build and run:

```bash
./build.sh --release --target libreshockwave_debugger --no-tests
./cmake-build-release/cpp/libreshockwave_debugger_app/libreshockwave_debugger [movie-or-recording]
```

Always test the desktop debugger in Release mode. Never use Debug mode. Pass a
movie or recording as the positional argument. `--play` starts ordinary playback
or recording replay after loading. It also works for asynchronous HTTP(S)
movies:

```bash
libreshockwave_debugger --play path/to/movie.dcr
libreshockwave_debugger --play path/to/session.lswdebug
```

Open local `.dir`, `.dcr`, `.dxr`, `.cct`, `.cst` files via **File → Open
Movie**. Open HTTP(S) movie URLs via **File → Open URL**. Set `key=value`
external parameters via **Parameters → Edit Parameters**. Reload the movie after
changing them. The window remembers recent movies, parameters, layout, and
breakpoints per movie.

### Replaying Recordings

- **Play & Record** saves stage mouse and keyboard input as a `.lswdebug` file.
  Input is flushed continuously (about once per second). So a crash mid-session
  still leaves a usable recording.
- Replay a recording with **File → Open Debug Recording**. Or open the
  `.lswdebug` from **File → Open Movie**. Or pass it on the command line with
  `--play`. The recording embeds the movie path or URL and the external
  parameters in its header. It reloads them. So a replay reproduces the same
  movie and connection parameters.
- Recordings are plain newline-delimited JSON (version 2). The first line is the
  header (`format`, `version`, `movie`, `externalParams`). Then one JSON object
  per event. Each event has `timeMs`, `type` (`mouseMove`/`mouseDown`/
  `mouseUp`/`keyDown`/`keyUp`), `stageX`, `stageY`, `keyCode`, `keyText`,
  `shift`, `ctrl`, `alt`, `rightButton`. An incomplete trailing line is ignored.
  Because recordings are plain JSON, you can hand-edit them. For example,
  retarget a click or reorder events. This builds focused diagnostics.
- Mouse-move events are throttled. A sample is at least every 50 ms when the
  position changes. Clicks and keys are recorded losslessly.
- Replay timing is relative to usable movie time. It is not wall-clock time.
  The replay clock only advances while the player is `networkReady()` and the
  movie is loaded. So events are never injected into an empty or loading score.
  The clock also pauses while a recording-triggered movie navigation fetches the
  next movie. Then it resumes when the new session is ready.
- Replays run against the live network. If the server data changed since
  recording, the recorded click path can diverge. It can give a different room
  list, a different navigation result, or a different handshake. Before assuming
  a renderer regression, check the dated notes in `docs/goals/` for past
  replay-divergence investigations.
- For visual verification of a replay, set `LIBRESHOCKWAVE_DEBUGGER_FRAME_DUMP`
  to a PNG path. It is a rolling frame snapshot, at most once per second.
  Optionally set `LIBRESHOCKWAVE_DEBUGGER_FRAME_STATS` for per-second
  presentation FPS. Set these before launching the debugger.

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
`MultiuserNetBridge` interface. But the two builds use different transports.
Know which one you are testing:

- **Browser/WASM**: the C++ side registers a `QueuedMultiuserBridge`. The host
  worker (`web/libreshockwave-cpp-worker.js`) polls connect and send requests
  (`lsw_poll_multiuser_requests`). It opens a **WebSocket** per connection. It
  uses `new WebSocket(ws[s]://host:port[/websocketPath])`. It uses `wss` when
  the page is HTTPS or `websocketSsl` is set. It uses binary frames and queues
  sends while CONNECTING. Incoming bytes are delivered back through
  `lsw_multiuser_message_bytes` and friends. The browser cannot open raw TCP
  sockets. So the WebSocket relay is the only transport there.
- **Native builds and the desktop debugger**: the plain `Player` registers
  `SocketMultiuserBridge`. It opens **raw TCP sockets** directly to the
  configured hosts. It uses `getaddrinfo` with `SOCK_STREAM`. Then
  `socket`/`connect`/`send`/`recv` on POSIX or Winsock. It has no WebSocket
  framing and no HTTP upgrade. The debugger connects straight to
  `connection.info.host:port` and the MUS port from the external parameters.

Implications:

- Verify networking behavior in the transport your change targets. A fix proven
  over WebSocket may not exercise the same framing or pacing code as raw TCP.
  And vice versa.
- A recording replayed in the desktop debugger uses raw TCP against the live
  server. The same movie in the browser uses the WebSocket relay. Reachability,
  packet pacing, and handshake behavior differ between the two.
- When diagnosing connection issues, state which transport was used. It is the
  WebSocket worker or the native TCP socket bridge.

## Verification Practice

- Verify with the existing tests that model the Director behavior in isolation.
  Do not encode asset names, member names, room names, or script-specific
  workarounds into tests. Do not add new tests (rule 9).
- For white-keying fixes, final visual acceptance covers all four canaries from
  the same rebuilt WASM bundle: Navigator, Messenger and Friends, Catalogue
  Collectables, and a loaded private room. For private-room regressions, use
  Firefox and geckodriver. Repeat the load because furniture timing can vary.
- For toolbar and icon repros, prefer a full mouse move, down, up, and click
  sequence at the stage coordinate. A synthetic click-only event can fail to
  trigger Director-style pressed and released handlers.
- Record white-pixel counts for the affected region and at least one adjacent
  control region when investigating white-keying regressions. Keep the expected
  image and actual canvas paths in the investigation note.
- Capture screenshots or state JSON for visual and browser verification when the
  acceptance criteria are visual or interactive.
- Keep these known generic fix areas generic. Preserve invalidation boundaries
  and Director semantics. Caches must not hide dynamic member creation, alias
  index imports, later top-level callbacks, or script-instance mutation. The
  areas are: VM fast prop-list object-call behavior for nested list and property
  access; XML Xtra and property traversal and Director-compatible collection
  semantics; script-instance property indexing and exact-name lookup;
  short-lived script-handler lookup caches scoped to a top-level handler;
  `script()` lookup caching by cast-library scope and normalized script name;
  member-registry alias refresh and fallback member-name resolution caching;
  generic list and prop-list immediate object-call handling;
  `StringBuiltins::offset` avoiding avoidable string copies; input hit selection
  preferring topmost interactive bounding-box hits where authored window element
  sprites need the event.
