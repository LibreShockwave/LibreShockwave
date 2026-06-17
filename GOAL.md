# Goal: Navigator Bitmap Cut-Off Native Parity

## Problem

In the v31 `venus-quackster-harness`, the Navigator is mostly rendering, but a
small bitmap in the Navigator/details area is visibly cut off compared to the
native Shockwave client. The supplied crop shows the issue around the Public
Spaces detail artwork: the traffic-light pole and avatar/artwork are present,
but the LibreShockwave output cuts the bitmap content differently from native.

Important clarification: `hh_navigator` is not itself cropping this bitmap.
Use the native image comparison as the authority for the expected bitmap extent
and placement. Investigate the generic Director/runtime rendering path that
feeds or displays this bitmap rather than assuming the Navigator script is
requesting a cropped result.

Existing evidence and comparison artifacts:

- Supplied issue crop:
  `artifacts/cropped.png`
- Current waited LibreShockwave capture:
  `artifacts/venus-quackster-harness-after-font-wait-canvas.png`
- Current state dump:
  `artifacts/venus-quackster-harness-after-font-wait-state.json`
- Current vs native RGB diff:
  `artifacts/after-font-fix-vs-native-rgb-diff.png`
- Existing focused crop comparisons:
  - `artifacts/whole-after-font-fix-crop-compare.png`
  - `artifacts/nav_details-after-font-fix-crop-compare.png`
  - `artifacts/nav_list-after-font-fix-crop-compare.png`
  - `artifacts/nav_header-after-font-fix-crop-compare.png`
  - `artifacts/nav_tabs-after-font-fix-crop-compare.png`
  - `artifacts/bottom_left-after-font-fix-crop-compare.png`
  - `artifacts/bottom_center-after-font-fix-crop-compare.png`
- Native target:
  `/opt/git/v31_room_load/v31_native.png`

## Harness

Use the normal harness for final acceptance:

```text
http://localhost:3000/venus-quackster-harness
```

After opening the harness, wait about one minute before judging or capturing
output. The initial `Starting...` frame is too early; the Director movie needs
time to load, initialize, and render the real Navigator state.

Always compare the waited harness capture with the native target using a pixel
diff. The acceptance target is exact native visual parity for the accepted
capture area against:

```text
/opt/git/v31_room_load/v31_native.png
```

`?debug=1` may be used only for diagnosis. The harness must keep the real
configured v31 hosts and ports:

```text
connection.info.host=verysecret.classichabbo.com
connection.info.port=30100
connection.mus.host=verysecret.classichabbo.com
connection.mus.port=38201
```

Do not replace them with localhost, mocked endpoints, alternate ports, or
committed credentials.

## Important Context

This is a bitmap/image-boundary parity issue, not a Navigator-specific crop
directive unless evidence proves otherwise. Likely owning layers include:

- Lingo image APIs such as `crop`, `copyPixels`, `duplicate`, `setAlpha`, and
  rect/quad handling.
- Runtime member image assignment and live bitmap invalidation.
- Bitmap decoding/import metadata, especially width, height, registration
  point, palette indices, matte/native alpha, and rectangular-media flags.
- Sprite baking and frame rendering geometry for runtime bitmap members.
- Matte/background-transparent preprocessing only if the cut-off is actually
  transparency removal rather than source/destination bounds.

Use the older Java reference implementation when useful:

```text
/opt/git/LibreShockwave-Java
```

Port root Director/runtime semantics into this C++ project. Do not copy in
scene-specific logic.

Relevant C++ areas to inspect first:

- `cpp/src/lingo/vm/OpcodeRegistry.cpp`
- `cpp/src/bitmap/Bitmap.cpp`
- `cpp/src/bitmap/Drawing.cpp`
- `cpp/src/player/render/pipeline/SpriteBaker.cpp`
- `cpp/src/player/render/pipeline/StageRenderer.cpp`
- `cpp/src/player/BitmapResolver.cpp`
- runtime member image handling in `cpp/src/cast/` and `cpp/src/player/`
- focused renderer/image tests in `cpp/tests/sdk_foundation_test.cpp`

Follow `docs/rendering-rules.md`, especially the rules for dynamic runtime
bitmaps, palette copies, matte/background-transparent ink, masks, quad
transforms, and avoiding post-render repairs.

## Constraints

- No harness shims, DOM/CSS overlays, post-processing pixel patches, or
  browser-specific hacks.
- No page-specific, room-specific, member-name-specific, user-name-specific, or
  v31-only branches.
- No `hh_navigator` special cases. Treat it as a consumer of generic Director
  runtime image behavior.
- No hardcoded authored Lingo methods or parent-script handlers in C++.
- No synthetic state seeded into Habbo scripts to force this scene to look
  correct.
- Do not fix the crop by expanding only this one visible bitmap or by editing
  screenshot pixels.
- Preserve existing working rendering: hotel exterior art, Navigator chrome,
  Public Spaces detail composition, bottom-left identity panel, window shadows,
  palettes, masks, text/font improvements, and room-loading behavior.

## Investigation Plan

1. Rebuild the current WASM output and capture the normal harness after a
   60-65 second wait.
2. Compare the waited capture with `/opt/git/v31_room_load/v31_native.png`,
   reusing the existing comparison artifacts and making a fresh crop around the
   affected Navigator detail bitmap.
3. Identify the exact sprite/member/runtime image that produces the cut-off
   detail artwork. Record its member name, cast lib/member number, sprite rect,
   loc, registration point, width, height, ink, blend, and whether it is static
   media or a script/runtime image.
4. Trace how that bitmap is produced: authored bitmap decode, imported image,
   `member.image`, `image.crop`, `copyPixels`, mask/matte generation, or another
   Lingo image operation.
5. Compare the C++ source rect, destination rect, image dimensions, anchor point,
   and matte/alpha metadata against native behavior and, where useful, the Java
   reference.
6. Determine whether the root cause is an off-by-one rect interpretation,
   clamped source bounds, dropped registration/anchor metadata, incorrect
   runtime-image size, copied palette/matte metadata, mask polarity, or final
   sprite rendering bounds.
7. Fix the generic owning layer only.
8. Add focused tests that reproduce the failing Director image semantics without
   naming Habbo assets, `hh_navigator`, or harness states.

## Progress / Findings

- 2026-06-17: Worktree started clean.
- 2026-06-17: Rebuilt the current WASM distribution successfully:
  `cmake --build cmake-build-wasm --target libreshockwave_cpp_wasm_dist -j2`.
- 2026-06-17: Rebuilt the native regression target successfully:
  `cmake --build cmake-build-debug --target libreshockwave_tests -j2`.
- 2026-06-17: Running `./cmake-build-debug/cpp/libreshockwave_tests` still
  aborts before completion at a pre-existing assertion:
  `cpp/tests/sdk_foundation_test.cpp:23825` in
  `testCastLibManagerFoundation()`, assertion
  `manager.getMemberProp(1, 4, "duration").intValue() == 1000`.
- 2026-06-17: Current local `http://127.0.0.1:3000/venus-quackster-harness`
  serves the expected Venus Quackster harness. A fresh waited browser capture
  was not produced in this session because the in-app browser was unavailable,
  Playwright/Puppeteer were not installed, and Chromium was not available; only
  Firefox was present.
- 2026-06-17: Produced additional ignored comparison crops from the existing
  waited artifact against `/opt/git/v31_room_load/v31_native.png`, including
  `artifacts/nav_detail_issue_raw-native-current-diff.png`, to isolate the
  Navigator detail-panel artwork/text region in raw 960x540 capture
  coordinates.
- 2026-06-17: Additional visible crop symptom: the Navigator list entry text
  `Welcome Lounge` gets cropped in our LibreShockwave output too. Specifically,
  the descender on the `g` in `Lounge` is cropped compared with native. Keep
  this as an explicit text-rendering parity check when evaluating the goal.
- 2026-06-17: Compared the C++ `Bitmap::getRegion`, image `crop`, and
  `copyPixels` anchor propagation paths against the Java reference. The checked
  C++ behavior currently matches the Java implementation: cropped images carry
  anchor points adjusted by the crop origin, and `copyPixels` seeds a destination
  anchor from the source anchor and source/destination rects when the
  destination has no anchor. No evidence yet justifies changing those generic
  semantics.
- 2026-06-17: Pixel inspection of the existing waited current/native crops for
  the `Welcome Lounge` row shows the LibreShockwave text ink is one row lower
  than native: the native dark text bounds in the inspected row are
  `(620,133)-(699,142)`, while the current bounds are `(620,134)-(699,142)`.
  The current output is therefore missing the descender row that should land at
  the bottom of the list row.
- 2026-06-17: Found a generic bitmap-font placement issue in
  `SimpleTextRenderer`: the default-leading case already excludes
  `topSpacing == 1` from line advance/height when `lineHeight == fontSize`, but
  still used that same pixel as a glyph top offset. That lowered bitmap glyphs
  by one pixel and can clip descenders when the rendered text is later copied
  into fixed-height Director UI rows.
- 2026-06-17: Implemented a generic bitmap-font fix in
  `cpp/src/player/render/output/SimpleTextRenderer.cpp` so default leading is
  treated as external leading instead of lowering the first glyph row. Added a
  focused synthetic regression in `testSimpleTextRendererFoundation()` that
  renders a 9-pixel-tall `g` under the default-leading condition and asserts the
  glyph occupies rows 0-8, leaving row 9 empty.
- 2026-06-17: The plain default-leading renderer change alone did not fix the
  real `Welcome Lounge` row in a fresh waited harness capture. That capture was
  saved as
  `artifacts/venus-quackster-harness-after-default-leading-fix-canvas.png`; the
  row still compared as native `(620,133)-(698,141)` versus current
  `(620,134)-(698,141)`.
- 2026-06-17: Found the actual `Welcome Lounge` path uses the embedded Volter
  TTF alias (`vb` -> bold Volter). A local probe showed the rendered
  `Welcome Lounge` ink occupied rows 1-9 before the TTF baseline adjustment,
  which explains why the bottom descender row was clipped by fixed-height
  Director UI rows.
- 2026-06-17: Implemented a generic TTF bitmap rasterizer baseline adjustment in
  `cpp/src/font/TtfBitmapRasterizer.cpp`, moving rasterized TTF glyphs up one
  pixel while preserving reported metrics. Added a focused embedded-Volter alias
  regression in `testBitmapFontAndFontRegistry()` that renders `Welcome Lounge`
  through `vb` and asserts the ink occupies rows 0-8 with row 9 empty.
- 2026-06-17: Captured a fresh waited harness with headless Firefox/Selenium
  after the TTF baseline fix:
  `artifacts/venus-quackster-harness-after-ttf-baseline-fix-canvas.png`. The
  focused `Welcome Lounge` comparison
  `artifacts/welcome_lounge_after-ttf-baseline-fix-native-current-diff.png`
  has zero differing pixels in the inspected crop, and the dark text bounds now
  match native exactly at `(620,133)-(698,141)`.
- 2026-06-17: The broader native parity goal is still not complete. The fresh
  TTF-baseline capture still differs from native in other regions:
  `artifacts/after-ttf-baseline-fix-vs-native-rgb-diff.png` reports 5,303
  differing RGB pixels over the 960x540 overlap, with remaining differences in
  Navigator/window and other UI areas. Do not mark the goal visually accepted
  yet.
- 2026-06-17: Rebuilt `libreshockwave_tests` successfully after the final
  renderer/rasterizer changes. Running
  `./cmake-build-debug/cpp/libreshockwave_tests` still aborts later at the same
  pre-existing `testCastLibManagerFoundation()` assertion, now at
  `cpp/tests/sdk_foundation_test.cpp:23902` because the added tests shifted line
  numbers. The new bitmap-font/default-leading and embedded-Volter regressions
  run before that later abort and therefore passed in this run.
- 2026-06-17: Rebuilt the WASM distribution successfully after the final changes:
  `cmake --build cmake-build-wasm --target libreshockwave_cpp_wasm_dist -j2`.
- 2026-06-17: Remaining fresh-capture diffs after the TTF-baseline fix are not
  all part of the original Navigator detail artwork issue. The two largest
  connected components are exterior cloud sprites in the hotel backdrop:
  `artifacts/remaining_component_01_native-current-diff.png` and
  `artifacts/remaining_component_02_native-current-diff.png`. Treat these as
  likely time/session-phase differences unless later evidence shows they are
  deterministic renderer regressions.
- 2026-06-17: The remaining scoped Navigator/detail artwork mismatch is around
  the traffic-light/figure matte-shadow edge, not the `Welcome Lounge` text.
  Focused artifacts:
  `artifacts/remaining_component_07_native-current-diff.png` and
  `artifacts/remaining_component_09_native-current-diff.png`. The visible issue
  is the cyan shadow/matte under the traffic-light/figure elements being shorter
  or shaped differently in LibreShockwave than native.
- 2026-06-17: Comparing the two fresh post-fix captures shows the traffic-light
  shadow region is stable across those runs, while the `Welcome Lounge` region
  differed only when an ineffective constrained-row experiment was applied and
  reverted. Continue the next investigation in matte/background-transparent
  handling or the runtime bitmap composition path for that detail artwork.
- 2026-06-17: Added a generic passive `debugSprites` bridge export for waited
  frame diagnostics. It reports baked sprite/member metadata plus compact color
  bounds; this is diagnostic-only and does not alter rendering.
- 2026-06-17: The remaining traffic-light/figure source is the runtime
  Navigator element sprite on channel 85:
  `Hotel Navigator_nav_roomnfo_icon`, `x=612`, `y=376`, `width=57`,
  `height=67`, `ink=8`, `blend=100`, `castMemberId=10072`. The Lingo path in
  `ParentScript 3 - Navigator Window Interface Class` builds this element from
  `member(tIconName).image`, `trimWhiteSpace()`, a centered 57x67 preview image,
  and `copyPixels(..., [#ink: 8])`.
- 2026-06-17: The likely selected source member for the default Public Spaces
  detail icon is `nav_ico_def_pr`:
  `/opt/git/v31_assets/hh_navigator/bitmaps/00396_nav_ico_def_pr.png`. The
  exported source and native icon slot both contain the cyan `#669999` shadow
  under the figure, but the baked LibreShockwave runtime sprite does not. Source
  and native have 57 `#669999` pixels at bounds `(22,55)-(39,62)` in the 57x67
  slot; the current rendered slot has none, and its alpha bounds stop at
  bottom 57 instead of preserving the lower outline/shadow.
- 2026-06-17: Tested a generic matte-key hypothesis: RGB and indexed matte
  preprocessing now prefer white/near-white edge canvas before falling back to
  dominant edge or index-0 matte, with focused synthetic regressions for
  black-outlined, white-canvas artwork preserving cyan shadow pixels. Native
  `libreshockwave_tests` builds and reaches only the known later
  `testCastLibManagerFoundation()` `duration == 1000` assertion, so those
  regressions pass.
- 2026-06-17: The matte-key hypothesis did not fix the waited Venus harness
  output. Fresh capture
  `artifacts/venus-quackster-harness-after-indexed-matte-fix-canvas.png` still
  has focused icon diff `artifacts/indexed_matte_fix/icon_native-current-diff.png`
  with 192 differing pixels, while the focused `Welcome Lounge` text crop
  `artifacts/indexed_matte_fix/welcome_lounge_native-current-diff.png` remains
  exact at 0 differing pixels. Continue from runtime member/feedImage/live-bitmap
  handling rather than assuming `copyPixels` matte key selection is the whole
  remaining cause.
- 2026-06-17: Added live dynamic-member diagnostics to the passive
  `debugSprites` bridge and rebuilt
  `cmake-build-wasm --target libreshockwave_cpp_wasm_dist` successfully. The
  waited harness capture and diagnostics are saved under
  `artifacts/runtime_live_diagnostics/`.
- 2026-06-17: The channel 85 Navigator icon sprite does have a dynamic runtime
  member and a 57x67 live runtime bitmap before baking:
  `artifacts/runtime_live_diagnostics/channel85.json`. The live bitmap is an
  opaque white-backed 57x67 image (`scriptModified=false`, alpha bounds
  `(0,0)-(57,67)`). Its top-color summary did not include `#669999`, while the
  baked bitmap alpha bounds stop at `(4,0)-(52,57)`. Because the top-color
  summary only reports the 16 most common colors, it is not enough by itself to
  prove that every cyan pixel is absent. It does prove the remaining mismatch
  occurs before or during the runtime image composition path that feeds
  `Hotel Navigator_nav_roomnfo_icon`, not during final sprite placement.
- 2026-06-17: The relevant Lingo path still points at
  `trimWhiteSpace()` followed by
  `tPrewImg.copyPixels(tTempImg, tdestrect, tTempImg.rect, [#ink: 8])` and
  `feedImage(tPrewImg)`. The next investigation should inspect the source image
  as decoded by LibreShockwave, including palette indices, and the MATTE
  `applyFloodFillTransparency` result used by the C++ image `copyPixels`
  dispatcher.
- 2026-06-17: A native throwaway probe against the real
  `/var/www/html/.../hh_navigator.cct` member `nav_ico_def_pr` found the C++
  decode is correct: 48x67, 8-bit, palette indices present, 57 `#669999`
  pixels at palette index 122, and `trimWhiteSpace()` returns the full
  `(0,0)-(48,67)` rect. A direct low-level
  `Drawing::copyPixels(..., InkMode::MATTE)` into a 57x67 32-bit white preview
  preserves all 57 cyan pixels. Therefore the source decode, palette index
  preservation through `getRegion`, and low-level MATTE copy are not sufficient
  to explain the harness crop.
- 2026-06-17: Cropping the source to the top 57 rows closely matches the live
  diagnostic's white/black counts and the baked alpha bottom of 57. Continue by
  tracing the actual runtime values for `tTempImg.width`, `tTempImg.height`,
  `tTempImg.rect`, `tPrewImg.rect`, `tdestrect`, and `tElement.getProperty(#depth)`
  in the Lingo image path, or by adding a generic image-operation trace that can
  record those values without hardcoding Navigator behavior.
- 2026-06-17: Added a bounded, generic image-operation diagnostic trace for
  `trimWhiteSpace()` and `copyPixels`, exposed through `debugImageTrace()`.
  Waited harness trace artifacts are in `artifacts/image_operation_trace/`.
  The trace proved the first Navigator detail composition is correct:
  `trimWhiteSpace()` returns a 48x67 source with 57 `#669999` pixels, and the
  48x67 -> 57x67 MATTE copy produces a 57x67 preview with all 57 cyan pixels.
  A later 57x67 -> 57x67 MATTE copy reduced cyan from 57 pixels to 2 because
  the 32-bit preview had inherited stale per-pixel palette indices from the
  8-bit source.
- 2026-06-17: Fixed the generic Lingo image `copyPixels` palette-index handling:
  deep destination images may inherit palette provenance from indexed sources,
  but no longer keep per-pixel palette indices, and palette-index preservation
  during copy now applies only to indexed destinations. Added a regression that
  copies an indexed matte source into a 32-bit wrapper and then MATTE-copies
  that wrapper again, preserving cyan shadow pixels both times.
- 2026-06-17: Rebuilt `libreshockwave_tests` and WASM successfully after the
  palette-index wrapper fix. Running `./cmake-build-debug/cpp/libreshockwave_tests`
  still aborts only at the known later
  `testCastLibManagerFoundation()` `duration == 1000` assertion, now at
  `cpp/tests/sdk_foundation_test.cpp:24007`, so the new image regression ran
  before that abort and passed.
- 2026-06-17: Fresh waited harness capture after the fix:
  `artifacts/palette_index_wrapper_fix/canvas.png`. Channel 85 now has all 57
  `#669999` cyan pixels in both live and baked bitmaps at bounds
  `(22,55)-(39,62)`, matching the native/source icon. Focused pixel diffs:
  `artifacts/palette_index_wrapper_fix/diffs/icon_native-current-diff.png` is
  0 differing pixels, and
  `artifacts/palette_index_wrapper_fix/diffs/welcome_lounge_native-current-diff.png`
  is also 0 differing pixels. The full-frame diff is still nonzero at 12,006
  pixels, and a broad Navigator-window crop
  `artifacts/palette_index_wrapper_fix/diffs/navigator_window_native-current-diff.png`
  is still nonzero at 7,268 pixels, so do not mark the broader visual parity
  goal complete yet.
- 2026-06-17: Added diagnostic-only probes to the passive `debugSprites` bridge
  so waited captures can report whether live/baked bitmaps still carry palette
  indices and can sample common edge pixels with palette-index provenance. This
  was used only to trace generic rendering state; it does not alter rendering.
- 2026-06-17: A synchronized waited capture before the next matte fix confirmed
  the broad Navigator mismatch was deterministic and not just a stale artifact:
  `artifacts/sync_navigator_probe_waited/canvas.png` had a broad Navigator
  window diff of 7,193 pixels, while both the focused icon crop and the focused
  `Welcome Lounge` crop were already exact at 0 differing pixels.
- 2026-06-17: The remaining broad Navigator mismatch was traced to generic
  MATTE inference treating near-white UI chrome (`#EFEFEF`) as transparent
  canvas. Tiny Navigator UI strips such as `nav_tb_tp` and `nav_tb_ed` are
  intentionally black plus `#EFEFEF`; treating `#EFEFEF` as matte left white or
  darker backing pixels in the composed runtime Navigator chrome.
- 2026-06-17: Fixed the generic matte inference to reserve white-edge canvas
  behavior for exact white (`#FFFFFF`) instead of near-white grays. This keeps
  exact-white transparent-canvas artwork working, while preserving authored
  near-white UI chrome such as `#EFEFEF`. Added synthetic regressions covering
  RGB, Lingo image, and indexed-palette 1x5 UI strips copied with MATTE ink.
- 2026-06-17: Rebuilt `libreshockwave_tests` and WASM successfully after the
  exact-white matte fix. Running `./cmake-build-debug/cpp/libreshockwave_tests`
  still aborts only at the known later
  `testCastLibManagerFoundation()` `duration == 1000` assertion, now at
  `cpp/tests/sdk_foundation_test.cpp:24055`, so the new matte regressions ran
  before that abort and passed.
- 2026-06-17: Fresh waited harness capture after the exact-white matte fix:
  `artifacts/near_white_matte_fix/canvas.png`. The sampled Navigator chrome
  strips now match native (`#EFEFEF` rows and black edge row), the focused
  Navigator icon crop remains exact at 0 differing pixels, and the focused
  `Welcome Lounge` text crop remains exact at 0 differing pixels. The broad
  Navigator-window crop improved from 7,268/7,193 differing pixels to 1,767
  differing RGB pixels, and the full-frame diff improved to 6,747 pixels.
- 2026-06-17: The broad Navigator goal is still not complete. Direct
  native/current component analysis for the
  `artifacts/near_white_matte_fix/canvas.png` Navigator crop (`360x410+580+80`)
  found 467 remaining components across 1,767 pixels. The largest component is
  a 100-pixel mismatch at global `(676,473)-(695,477)` involving native gray
  pixels versus current magenta/black pixels near the lower Navigator area. Most
  other components are tiny text/edge swaps around the top tabs and room list.
  Continue with focused Navigator and text rendering pixel diffs before marking
  the goal complete.

## Acceptance Criteria

- A rebuilt `http://localhost:3000/venus-quackster-harness` screenshot after the
  normal wait matches the native Navigator/detail bitmap extent and placement.
- Completion requires a saved pixel-diff comparison against
  `/opt/git/v31_room_load/v31_native.png`; exact parity for the accepted capture
  area is the target.
- Completion must include focused pixel-diff checks for both the Navigator
  artwork/chrome and text rendering regions, not just a visual inspection or
  full-frame summary, to prove the Navigator detail bitmap and text placement
  match native.
- You must pixel-diff both the Navigator region and the text-rendering region
  before marking the goal complete. The text diff must cover `Welcome Lounge`
  closely enough to catch the cropped `g` descender.
- Do not mark the goal complete until those focused Navigator and text
  rendering pixel diffs have been saved and verified.
- The affected Navigator detail bitmap is no longer cut off compared to native.
- The Navigator remains functional and the `Welcome Lounge` `Go` flow still
  loads the room.
- The Public Spaces detail artwork, bottom-left identity panel, bottom-center
  text, Navigator chrome, masks, shadows, palettes, and exterior art do not
  regress.
- No hardcoded movie, member, room, user, Navigator, or harness-specific logic is
  introduced.
- Focused image/runtime/rendering tests cover the fixed behavior.
- Adjacent rendering regression coverage is run or explicitly documented:

```bash
cmake --build cmake-build-debug --target libreshockwave_tests -j2
cmake --build cmake-build-wasm --target libreshockwave_cpp_wasm_dist -j2
```

If the full native test binary still hits a known pre-existing assertion,
document the exact assertion and run focused tests covering the image/rendering
change.

## Non-Goals

- Do not tune only the visible Navigator detail bitmap.
- Do not assume `hh_navigator` is the crop source without tracing evidence.
- Do not introduce browser rendering or CSS/DOM patches in place of Director
  bitmap rendering.
- Do not mask the issue with screenshot-specific transparency cleanup,
  nearest-neighbor cleanup passes, or hand-expanded bitmap bounds.
- Do not regress the previous font/text parity work.
