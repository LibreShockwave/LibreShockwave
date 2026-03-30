# Text Wrapper Glyph Clipping

## Problem

Some Fuse and Habbo-style text wrappers rendered white text with missing left strokes or broken first letters after `MATTE` compositing. The visible failures were strongest in black-backed toolbar labels such as `How To Get?`, `Habbo Club`, and similar UI strings.

## Symptom Pattern

- The bug showed up most clearly in white-on-black text wrapper images.
- The first glyph in a line was often the most damaged.
- `MATTE` looked suspicious because archived Fuse wrappers finish with `copyPixels(..., [#ink: 8])`.
- The damage persisted even after preserving font bearings, which meant the wrapper pipeline still had a second failure after text rasterization.

## Root Cause

The live bug was the interaction between text-member `.image` rendering and the `MATTE` copy path:

- Archived Fuse wrappers set `pTextMem.bgColor`, but they also fill the destination image themselves before copying `pTextMem.image` with `[#ink: 8]`.
- That usage pattern depends on `text member.image` behaving like transparent text artwork rather than an opaque text rectangle.
- Our runtime still rendered non-white text-member backgrounds as opaque pixels, so wrapper text images carried a solid black field behind the white glyphs.
- On top of that, `Drawing.copyPixels(..., MATTE, ...)` ignored native alpha and always forced RGB flood-fill matte extraction for 32-bit sources.
- The RGB fallback assumes white matte, so any white glyph pixels that touched the image edge through the wrapper's tight text bounds were stripped as matte-connected background.

The earlier TTF bearing fix was still correct and remains necessary, but it was not sufficient on its own. The real wrapper regression required fixing the text-member image contract and the `MATTE` native-alpha path together.

## Proper Fix

- Render `CastMember.renderTextToImage()` as transparent text art for the text-member `.image` property, regardless of the member's configured `bgColor`.
- Keep explicit sprite-text rendering on the separate `renderTextToImage(width, height, bgColor)` path so score sprites can still request opaque backgrounds.
- Key the cast-member text cache by `bgColor` as well as dimensions so transparent member images do not leak into opaque sprite renders.
- Make `Drawing.copyPixels(..., MATTE, ...)` honor native alpha directly before falling back to RGB flood-fill matte extraction.

## Files Involved

- `player-core/src/main/java/com/libreshockwave/player/cast/CastMember.java`
- `sdk/src/main/java/com/libreshockwave/bitmap/Drawing.java`
- `player-core/src/test/java/com/libreshockwave/player/cast/CastMemberTextImageTest.java`
- `sdk/src/test/java/com/libreshockwave/bitmap/DrawingMatteTest.java`

## Validation

- `./gradlew :sdk:test --tests com.libreshockwave.bitmap.DrawingMatteTest`
- `./gradlew :player-core:test --tests com.libreshockwave.player.cast.CastMemberTextImageTest --tests com.libreshockwave.player.render.output.SimpleTextRendererTest`
- `./gradlew :player-wasm:generateWasm`

## Note On Fuse Behavior

Archived Fuse `ParentScript 60 - Text Wrapper Class.ls` still uses `copyPixels(..., [#ink: 8])` for wrapper output after optionally filling the destination background itself. The important correction is that the runtime now matches that contract more closely: `pTextMem.image` supplies transparent text art, and `MATTE` respects native alpha instead of re-keying those images against white.
