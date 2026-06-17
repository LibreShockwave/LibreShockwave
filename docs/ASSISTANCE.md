# Assistance Notes

This file is a quick orientation guide for creating future goal files in this
repository. It summarizes recurring constraints, local helper repositories, and
verification expectations that should be copied into new goals when relevant.

## v31 Harness Context

Many recent goals have used the v31 `venus-quackster-harness` login and native
parity flow:

```text
http://localhost:3000/venus-quackster-harness
http://localhost:3000/venus-quackster-harness?debug=1
```

The harness must use the real configured hosts and ports:

```text
connection.info.host=verysecret.classichabbo.com
connection.info.port=30100
connection.mus.host=verysecret.classichabbo.com
connection.mus.port=38201
```

Do not replace these with localhost, alternate ports, mocked endpoints, or
committed credentials.

Historical and completed goal notes live in `docs/goals/`:

- `2026-06-15-venus-quackster-harness-native-parity.md`: native parity target
  for the bottom-left Habbo identity figure and Navigator Public Spaces image.
- `2026-06-15-login-habbo-head-icon.md`: completed diagnosis and fix path for
  the missing bottom-left head icon. The fix was generic Director-compatible
  collection/property semantics, not Habbo-specific code.
- `2026-06-16-welcome-lounge-fps-performance.md`: current performance context
  and the strongest guardrail against hardcoding authored Lingo methods into
  C++.
- `2026-06-16-welcome-lounge-go-after-proclist-removal.md`: completed
  verification that `Go` reaches Welcome Lounge without restoring the
  hardcoded `pProcList` workaround.

## Hard Rules From Goal Files

- Do not add client shims or harness-specific workarounds.
- Do not hardcode movie-specific Lingo implementations in C++.
- Do not add page-specific, room-specific, member-name-specific, user-name
  specific, or harness-only fixes.
- Keep fixes in the C++ runtime, VM, Xtra, networking, cast/member, input, or
  renderer layer that owns the actual failing behavior.
- Treat app-authored Lingo as app-authored Lingo. The runtime must not grow
  C++ branches keyed to v31 parent-script names, movie-script names, handler
  names, Resource API helper names, or room/login script names.
- BigInt/HugeInt/HugeInt15 is the canonical example: do not implement native
  C++ branches for `BigInt`, `HugeInt`, `HugeInt15`, `powMod`, `Modulo`, `div`,
  `getString`, or `getByteArray`.
- The same rule applies to other hot asset handlers such as `getmemnum`,
  `memberExists`, `updateProcess`, `createPassiveObject`, `solveMembers`, and
  `parseCallback`.
- Performance improvements must be generic VM/runtime work: Director
  semantics, bytecode dispatch, handler dispatch, list/property-list behavior,
  script-instance properties, arithmetic, strings, builtins, data structures,
  cooperative scheduling, or cache behavior.
- Do not restore C++ hardcoding for `pProcList`, `registerProcedure`, or
  `removeProcedure`.
- Do not seed synthetic broker instances with asset-owned state such as
  proc-list templates or fixed event maps. If broker support is needed,
  preserve and dispatch real authored broker script instances and their
  properties.
- Do not commit real SSO tickets or sensitive runtime credentials.
- Commit messages must not mention game, asset, harness, room, user, or
  endpoint names. Describe the generic runtime behavior being fixed instead.

## Harness Workflow

For v31 harness work, use the normal URL for final acceptance:

```text
http://localhost:3000/venus-quackster-harness
```

Use `?debug=1` only for diagnostics. Acceptance evidence for visual parity
should come from the normal harness unless the goal explicitly says otherwise.

Typical verification sequence:

1. Rebuild native tests and the WASM distribution as needed.
2. Serve the WASM dist, usually from:

```text
/opt/git/LibreShockwave/cmake-build-wasm/cpp/wasm-dist
```

3. Open the normal harness URL and wait long enough for login to finish.
4. Confirm the hotel exterior loads with the Navigator open.
5. Confirm the bottom-left identity panel shows the Habbo head/figure next to
   `Quackster`.
6. Confirm the Navigator details pane shows the Public Spaces illustration.
7. For Welcome Lounge goals, select `Welcome Lounge`, click `Go`, and confirm
   the room loads and remains functional.

Relevant build targets:

```bash
cmake --build cmake-build-debug --target libreshockwave_tests -j2
cmake --build cmake-build-wasm --target libreshockwave_cpp_wasm_dist -j2
```

The full native test binary has had a known pre-existing abort around a
`sdk_foundation_test.cpp` `gcCallbacks == 1` assertion. If it still appears,
document the exact assertion and run the focused tests that cover the current
change.

If browser results are ambiguous, restart the static server that serves
`cmake-build-wasm/cpp/wasm-dist` after rebuilding so deleted old bundle file
descriptors cannot confuse the run.

## Important Local Paths

Primary repository:

```text
/opt/git/LibreShockwave
```

Nearby asset and reference repositories:

```text
/opt/git/v31_assets
/opt/git/v14_assets
/opt/git/v1_assets
/opt/git/v1_assets/handshake-demo
/opt/git/LibreShockwave-Java
/opt/git/test/LibreShockwave
```

Native visual references mentioned by goals:

```text
/opt/git/v31_room_load/v31_native.png
/opt/git/v31_room_load/v31_welcome_lounge.png
/home/alex/Pictures/Screenshots/Screenshot_20260615_215344.png
```

Use these references only as evidence and comparison targets. Do not encode
their specific scene names or member names into generic runtime behavior.

## v31 Assets

`/opt/git/v31_assets` is the main local source for v31 cast exports and Lingo
dumps. Its `README.md` describes the extraction layout:

- `bitmaps/`: decoded PNG bitmap members.
- `text/`: STXT and XMED text payloads, including `.window`, `.props`,
  `.index`, and config-like text members.
- `sounds/`: WAV/MP3 sound exports.
- `palettes/`: palette TSV files.
- `raw_chunks/`: member-owned raw resource chunks.
- `manifest.tsv`: asset/member mapping.
- `file_info.tsv`: parsed file metadata.
- `projectorrays_lingo/<file>/casts/**/*.ls`: ProjectorRays Lingo dumps.

Frequently relevant v31 paths:

```text
/opt/git/v31_assets/hh_interface.cct
/opt/git/v31_assets/hh_interface
/opt/git/v31_assets/projectorrays_lingo
/opt/git/v31_assets/projectorrays_lingo/fuse_client
/opt/git/v31_assets/projectorrays_lingo/hh_interface
/opt/git/v31_assets/projectorrays_lingo/hh_interface.cst
/opt/git/v31_assets/projectorrays_lingo/hh_entry_init/casts/External/ParentScript 5 - HugeInt15.ls
/opt/git/v31_assets/projectorrays_lingo/hh_entry_init/casts/External/ParentScript 8 - Login Handler Class.ls
/opt/git/v31_assets/projectorrays_lingo/fuse_client/casts/External/ParentScript 51 - Connection Instance Class.ls
/opt/git/v31_assets/projectorrays_lingo/fuse_client/casts/External/ParentScript 52 - Multiuser Instance Class.ls
/opt/git/v31_assets/projectorrays_lingo/fuse_client/casts/External/BehaviorScript 3 - Event Broker Behavior.ls
```

The bottom-left head icon investigation identified this authored call path:

```text
updateEntryBar
createMyHeadIcon
Figure_Preview.createHumanPartPreview
Figure_Preview.getHumanPartImg(#head, ...)
Human Class EX.getPartialPicture(#head, ...)
```

The Welcome Lounge networking and performance investigations identified these
authored paths:

```text
Connection Instance Class.xtraMsgHandler
Login Handler Class.responseWithPublicKey
Login Handler Class.handleServerSecretKey
HugeInt15.powMod
HugeInt15.Modulo
HugeInt15.div
HugeInt15.getString
HugeInt15.getByteArray
```

Those paths are useful for diagnosis, but they must stay authored Lingo. Fix
the generic runtime behavior underneath them.

## Other Local References

`/opt/git/v14_assets` and `/opt/git/v1_assets` have the same extraction shape
as `v31_assets`. They are useful for comparing older Habbo asset behavior and
Director extraction output.

`/opt/git/v1_assets/handshake-demo` contains a minimal Node.js TCP demo server
for the v1 EnterpriseServer-style handshake. It only gets the client out of the
connection-wait loop and intentionally does not implement login, rooms,
messenger state, or gameplay.

`/opt/git/LibreShockwave-Java` is the older Java LibreShockwave project. Its
README documents the Java parser/player/editor split, and
`docs/rendering-rules.md` contains an earlier version of the renderer rules
that were later carried into this C++ repository.

`/opt/git/test/LibreShockwave/docs/private-room-wall-investigation.md` contains
older investigation notes for private room wall rendering. The key lesson is to
trace sprite references and authored initialization paths before changing broad
rendering behavior.

## Rendering Rules

The current C++ rendering guide is `docs/rendering-rules.md`. Follow it when
touching image creation, palette state, copy paths, matte/background
transparent ink, masks, text remaps, dynamic runtime bitmaps, window buffers,
or quad transforms.

Core rendering principles:

- Prefer root-cause fixes in image storage, VM image methods, ink handling, and
  copy paths over post-render pixel repair.
- Keep indexed-image metadata and ARGB pixels consistent.
- Preserve authored transparency, masks, palette indices, and matte behavior.
- Avoid broad heuristics that erase ordinary light pixels, text, highlights, or
  overlays.
- For script-created UI surfaces, inspect the baked `RenderSprite`, dynamic
  `CastMember`, and runtime bitmap before changing final transparency rules.
- A missing sprite can be a bake-source or runtime-bitmap selection problem, not
  a matte problem.
- Dynamic runtime bitmaps can be authoritative even when
  `Bitmap::isScriptModified()` is false.
- Add focused tests for visible pixels and palette indices when touching shared
  copy or rendering behavior.

## Known Generic Fix Areas

The existing goal notes record several generic fixes that should stay generic:

- VM fast prop-list object-call behavior for nested list/property access.
- XML Xtra/property traversal and Director-compatible collection semantics.
- Script-instance property indexing and exact-name lookup.
- Short-lived script-handler lookup caches scoped to a top-level handler.
- `script()` lookup caching by cast-library scope and normalized script name.
- Member-registry alias refresh and fallback member-name resolution caching.
- Generic list and prop-list immediate object-call handling.
- `StringBuiltins::offset` avoiding avoidable string copies.
- Input hit selection preferring topmost interactive bounding-box hits where
  authored window element sprites need the event.

When extending these areas, preserve invalidation boundaries and Director
semantics. Caches should not hide dynamic member creation, alias index imports,
later top-level callbacks, or script-instance mutation.

## Documentation And Investigation Practice

- Read the relevant dated file in `docs/goals/` before drafting a new goal.
- Search with `rg` before assuming a behavior is missing.
- When an issue reproduces only in v31, inspect the extracted `.ls`,
  `.lasm`, `.window`, `.props`, `manifest.tsv`, and bitmap/text exports before
  editing C++.
- Use app-authored Lingo paths to locate the failing generic runtime behavior.
- After reading the relevant assets, make a separate focused regression test
  that asserts the generic runtime behavior the assets depend on. Do not encode
  the asset names, member names, room names, or script-specific workaround into
  the test; model the Director behavior in isolation.
- Capture screenshots or state JSON for visual/browser verification when the
  acceptance criteria are visual or interactive.
- If a diagnostic hardcoded path is used briefly to prove a cause, remove it
  before finalizing. The permanent fix must obey the generic-runtime rules
  above.
