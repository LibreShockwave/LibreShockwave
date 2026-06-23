# Rendering Rules So Far

These notes capture C++ renderer behavior that has been validated while fixing
image, ink, palette, matte, mask, dynamic runtime image, and quad-copy
regressions. They are intended as rules for future fixes and tests, not as a
complete rendering specification.

## General Approach

- Prefer root-cause fixes in image storage, VM image methods, ink handling, and
  copy paths over post-render pixel repair.
- Keep fixes in the C++ runtime, renderer, VM, and cast/member layers. Do not
  compensate with client-side shims, harness-specific patches, or hardcoded
  Lingo implementations.
- Keep indexed-image metadata and rendered ARGB pixels consistent. If a path
  changes visible pixels on an indexed image, it may also need to update
  `paletteIndices`.
- Preserve authored transparency and masks. Avoid broad heuristics that turn
  ordinary light pixels into transparent pixels.
- When changing one rendering path, rerun focused tests for adjacent behavior:
  palette copies, matte/background-transparent ink, masks, quad transforms, and
  text or color remap.
- If a visual regression appears in a script-created UI surface, inspect the
  baked `RenderSprite`, its dynamic `CastMember`, and the member's runtime
  bitmap before changing matte heuristics. A missing sprite can be a bake-source
  selection problem, not a transparency problem.

## Image Creation And Palette State

- `image(width, height, depth[, palette])` starts as opaque white.
- For indexed images, attach the palette before the initial white fill so the
  backing pixels and palette indices are initialized together.
- `fill(..., paletteIndex(n))` on indexed images must update both the ARGB pixel
  data and `paletteIndices`.
- Integer palette-index fills on images with depth `<= 8` should be treated as
  indexed fills.
- RGB fills on 32-bit images that carry a palette reference should resolve RGB
  normally without creating an indexed image.

## Palette Index Preservation

- Preserve source palette indices when copying from an indexed source into a
  compatible indexed destination for full-blend `COPY`, `MATTE`, and
  `BACKGROUND_TRANSPARENT` operations with no mask or remap.
- For `BACKGROUND_TRANSPARENT`, transparent source pixels must not overwrite the
  destination pixel or its destination palette index.
- When copying RGB or non-indexed source pixels into an indexed destination that
  already has palette indices, refresh indices only inside the copied rectangle
  through the destination palette.
- Do not clear the entire destination index plane just because part of an
  indexed destination received RGB pixels.
- When copied RGB white lands on an indexed destination, keep an existing index
  `0` only when the destination already had index `0` and palette entry `0` is
  white. Otherwise choose the nearest non-matte palette index so ordinary white
  text or panel pixels do not become holes in later matte operations.

## Quad Copying

- Quad destinations use this point order: top-left, top-right, bottom-right,
  bottom-left.
- Axis-aligned quads cover identity, flips, and 90-degree transforms.
- Preserve transformed source palette metadata and indices when available.
- The fast quad path may handle plain `ink` and `blend`. Prop lists containing
  masks, darken or lighten behavior, remaps, or other copy properties should
  fall back through rectangular `copyPixels` so the existing property semantics
  remain active.
- Do not route all quads through the rectangular path. Landscape and mask
  transforms depend on the quad path keeping its transform behavior.
- Scaled copies must scale palette indices with pixels when indices are
  preserved.

## Matte And Background-Transparent Ink

- `MATTE` ink uses a flood-fill matte from border-connected background pixels.
- If valid palette indices exist, prefer palette-index matte detection. Border
  index `0` white is normal matte/background when present and the image is not
  uniform.
- For script-built indexed UI buffers, sprite-level `backColor` is still
  meaningful for final `MATTE`. If it resolves to white through the active
  palette or as packed RGB, prefer the matching indexed white matte before the
  dominant-edge heuristic. This prevents dark shadow art on the edges from being
  mistaken for the matte color while the original white backing remains visible.
- Without valid indices, use the RGB flood-fill fallback, usually keyed from
  white.
- `BACKGROUND_TRANSPARENT` ink defaults the background key to white unless
  `bgColor` provides a different key.
- `BACKGROUND_TRANSPARENT` copies from indexed sources with the default white
  key should skip the authored matte index when palette index `0` is near-white,
  but must not erase duplicate white or near-white RGB pixels that came from
  other palette entries.
- Background-transparent copies should treat border-connected key pixels as
  transparent while preserving enclosed key-colored content when required.
- Script-built runtime backings under `BACKGROUND_TRANSPARENT` need provenance
  from the Lingo window framework plus copy geometry and source content. Do not
  treat every white `image.fill()` as backing that should survive final baking.
  In the v31 fuse_client framework, `Image Wrapper Class.feedImage()` is the
  path that fills `pBuffer.image` and then renders a source image into it. Only
  that transaction may mark a possible preserved fill backing.
- Not every `feedImage` result should preserve white. Navigator text/link
  surfaces are also `#type: "image"` wrappers fed with writer-rendered images;
  their white is a matte and must still key out. Preserve the wrapper fill only
  when the source is a script-created rectangular image copied with
  `BACKGROUND_TRANSPARENT`, the requested source rectangle extends beyond the
  source image, and the visible source looks like sparse viewport/list content
  rather than a text/link matte or artwork slot. Full-size catalogue artwork,
  product strips, Navigator text/link mattes, untouched `image()` default
  white, generated text mattes, and room/compositor surfaces must stay on the
  normal white-keying path.
- Near-white matte preprocessing must stay narrow. Apply it only when the source
  has a border-connected near-white matte and real non-near-white content. A
  broad near-white rule can erase legitimate light text, highlights, and
  overlays.
- Outlined-white-body preservation is not a general script-buffer rule. Keep it
  for authored low-depth assets and explicitly scoped script-built 32-bit chat
  backgrounds. Do not apply it to script-modified indexed window buffers, where
  white is usually the matte backing.

## Window Buffers And Grouped UI

- Habbo window layouts group repeated element IDs into one bitmap buffer per
  group. In `habbo_simple.window`, `shadow` is built before `back`; the shadow
  group uses mixed item inks with a shared blend, so the final sprite stays
  `MATTE` and receives the shared blend.
- When all items in a group share `blend`, item-level copies into the group
  buffer should render at full strength and the final sprite should carry the
  shared blend. For the loader/window shadow this means black shadow pixels are
  stored opaque in the buffer, then the whole sprite blends at 30 percent.
- Script-created indexed window buffers start as opaque white with palette
  indices initialized. The final `MATTE` pass must remove that white backing so
  only the authored shadow or chrome pixels remain. If the white backing survives
  and the sprite has partial blend, it appears as a grey halo on black stage
  backgrounds.
- Window shadow artifacts are usually a matte/index problem before they are a
  z-order problem. Verify the baked shadow bitmap first: black shadow pixels
  should remain opaque, and edge-connected white backing should become fully
  transparent.

## Masks

- `createMatte()` and `createMask()` are related but not interchangeable.
- `createMatte()` creates alpha-style matte data from native alpha when present,
  otherwise from flood-filled background.
- `createMask()` can create direct mask images for mask-source content. For
  ordinary images it falls back to flood-filled matte behavior.
- `maskImage` uses luma-style mask semantics: white blocks drawing and black
  allows drawing. In code, `Drawing.maskAllowsPixel(mask, x, y)` is true when
  `maskAlphaFromPixel(pixel) < 255`.
- Explicit `#maskImage` properties must remain honored. Native-alpha source
  images do not automatically invalidate authored masks.
- When scaling with a mask, evaluate the mask at original source coordinates.

## Native Alpha

- A 32-bit member is opaque unless the bitmap carries native-alpha metadata.
- Native-alpha bitmaps normally use alpha for transparency, but an opaque
  border key can still be keyed by `BACKGROUND_TRANSPARENT`.
- Non-native alpha zeroes from container data should be exposed as opaque before
  script-level operations that expect non-alpha 32-bit image behavior.

## Text, Remap, And Color

- `#color` and `#bgColor` remap should apply only to mostly grayscale sources.
  Already-colored images or text should keep their authored RGB values.
- White-backed grayscale text copied into a compatible destination may treat
  white as transparent.
- Near-white text glyphs and overlays must not be stripped by matte heuristics.
- In the C++ `SpriteBaker`, live runtime images for text members take precedence
  over text re-rendering. If a field/text member has a runtime bitmap supplied
  by the live bitmap provider, bake that image through the normal live-bitmap
  processing path instead of invoking `TextRenderer`.

## Dynamic Runtime Bitmaps

- A dynamic runtime bitmap can be authoritative even when
  `Bitmap::isScriptModified()` is false. Runtime-created members and script-fed
  UI buffers may carry valid pixel content without that flag.
- In `SpriteBaker::bakeBitmap`, use the live bitmap provider for dynamic
  members when the live bitmap has meaningful dimensions, not only when
  `isScriptModified()` is true. This is required for script-built UI images such
  as the Habbo Navigator Public Spaces illustration.
- Preserve the existing `processLiveBitmap` path for runtime images so
  background-transparent and matte handling remains centralized.
- If preserving opaque script-built runtime backing pixels, use runtime
  provenance from the Lingo image operations, the current window-wrapper path,
  the `copyPixels` source/dest geometry, and source content. Verify the target
  UI window and adjacent always-visible surfaces such as the Navigator,
  catalogue pages, or loaded room playfield.
- Do not add special cases for individual member names, room names, or harness
  states. If a runtime bitmap is present but not rendering, fix the generic
  dynamic-member bake path.
- If a runtime bitmap bakes as fully transparent, inspect the source runtime
  bitmap first. An all-white source after script composition means the producer
  path failed; changing final matte handling would only hide the real bug.

## Darken, Lighten, And Color Ramps

- For `DARKEN` and `LIGHTEN`, opaque white 32-bit non-alpha buffers can be
  neutral content, not automatic matte.
- Dynamic indexed sprites that depend on color ramps require preserved palette
  indices. Later text, timestamp, or wrapper copies must not clear base image
  indices.
- Indexed darken color-ramp exactness still needs care when future work touches
  palette-index copy or refresh paths.

## Verification

- Prefer focused tests that recreate script image operations and assert both
  visible pixels and palette indices where relevant.
- For `SpriteBaker` changes, add tests for authored bitmap members, dynamic
  bitmap members, authored text members, and dynamic text members when the
  change affects live bitmap selection.
- When changing white-keying or runtime-backing preservation, include
  regression cases for a marked image-wrapper sparse viewport composite that
  should remain opaque and full-size artwork slots, Navigator text/link mattes,
  product/list strips, or stage-sized runtime surfaces that should still key
  white transparent.
- For v31-style window-buffer fixes, verify at least these surfaces together:
  Navigator Public Spaces/Rooms text areas, Messenger/Friends after opening the
  toolbar icon, Catalogue Collectables product strip, and the private room
  playfield/furniture load. A preservation rule that fixes only Messenger can
  easily reintroduce broad white fills elsewhere.
- Use pixel counts around the target region and at least one adjacent control
  region. A fix that restores one window can still be wrong if it introduces
  large white fills elsewhere in the same frame.
- Include a non-`scriptModified` dynamic runtime bitmap test. The expected
  behavior is that a meaningful dynamic runtime image still bakes.
- Keep landscape and mask regression coverage in the test set when touching
  quad-copy, mask, matte, or background-transparent paths.
- Run narrow tests for the affected behavior first, then broader bitmap or VM
  suites when the change touches shared copy or image storage code.
