# Rendering Pipeline

## 1. Rendering Philosophy

LibreShockwave renders a movie by translating Director score state and runtime sprite mutations into a baked, immutable frame snapshot. The rendering pipeline is software-driven and compatibility-oriented. It is not an AWT-first scene graph and it is not a browser DOM renderer.

The current rendering stack is best understood as:

`score + runtime sprite state -> baked sprite descriptors -> software compositing -> frame snapshot`

## 2. Pipeline Stages

`FrameRenderPipeline` is the high-level coordinator for a frame render. Its work can be summarized in five stages:

1. Collect score-backed sprites from the current frame.
2. Collect dynamic or puppeted sprites that also need rendering.
3. Sort sprites by effective depth using `locZ`, then by channel.
4. Bake each sprite into a drawable representation.
5. Publish the baked set to the stage and produce a `FrameSnapshot`.

This structure is important because it separates state resolution from pixel generation. The player first decides what exists, then decides how each thing should look, then composites the result.

## 3. Sprite State Resolution

`StageRenderer` owns the runtime `SpriteState` registry. That registry preserves per-channel mutable values such as:

- location and size
- visibility
- `locZ`
- ink and blend
- foreground and background colors
- transform state such as flip, rotation, stretch, and skew
- puppeting flags
- dynamic member overrides
- script instances attached to the sprite

For score-backed sprites, authored channel data remains the baseline. For dynamic and puppeted sprites, runtime state can diverge from authored values. The renderer does not simply reread score data every frame and discard mutations.

When a score identity changes for a non-puppeted sprite, the runtime state can be rebound to the new score definition. When a sprite is still logically the same score occupant, the state is synced rather than recreated.

## 4. Bake Step

`SpriteBaker` is the subsystem that turns sprite/member state into renderable bitmap output. It currently handles at least these categories:

- bitmap members
- text and styled text members
- vector-like shape members
- film loops

The bake step is where many Director-specific behaviors become concrete.

### 4.1 Bitmap Sprites

File-backed bitmap members usually pass through `BitmapCache` so that palette conversion and ink preprocessing are not repeated unnecessarily.

The cache key is based on render-relevant identity such as:

- member identity
- ink mode
- foreground and background color
- palette version

Dynamic bitmaps are handled differently because their contents can change at runtime. Those are not treated as safely immutable cache entries.

### 4.2 Text Sprites

Text rendering is not a side feature. It has its own compatibility rules and fallback paths.

Current behavior includes:

- use runtime text content when a cast member has been modified dynamically
- fall back to file-backed STXT or XMED data when runtime text is absent
- support transparent text backgrounds for `BACKGROUND_TRANSPARENT`
- reuse rendered text images through cast-member-level caching where valid

`SimpleTextRenderer` is installed into the player and acts as the active text rasterizer. That means text output is part of the emulator's deterministic software path, not delegated to a random platform text widget.

### 4.3 Shape Sprites

Shape members can render from authored shape metadata when present. When that metadata is incomplete or the shape is effectively a solid region, the bake path can fall back to direct fill behavior.

### 4.4 Film Loops

Film loops are composited by resolving the loop's embedded score content, rendering its internal sprites, and then applying the outer sprite's own ink and placement rules. The loop is therefore not just "another bitmap". It is a nested render pass.

## 5. Final Stage Composition

`SoftwareFrameRenderer` performs final frame composition into a flat pixel buffer.

Key properties of the current implementation:

- pure software rendering
- stage background fill when no explicit stage image exists
- optional use of a live stage image buffer for `(the stage).image`
- ordered sprite compositing after bake
- scaling, flipping, and alpha-aware composition in the software path

This renderer handles a meaningful set of Director-style inks, including normal alpha-style composition and special modes such as:

- `ADD`
- `SUBTRACT`
- `DARKEST`
- `LIGHTEN` and `LIGHTEST`
- `REVERSE`
- `GHOST`
- `NOT_*` variants

Those ink modes are not cosmetic details. A large share of visual compatibility lives or dies in this layer.

## 6. Registration Points, Depth, And Geometry

Sprite placement is not based solely on top-left score coordinates. The renderer applies registration-point-aware positioning, transform state, scale handling, and mirroring rules before final composition.

The effective ordering is:

- first by `locZ`
- then by channel as a stable tie-breaker

This means runtime depth mutations can move a sprite ahead of or behind authored neighbors without modifying the authored score itself.

## 7. Stage Image And Readback

The stage can expose a live image buffer through movie properties. When `(the stage).image` is used, the renderer treats that as an alternate backing surface instead of a simple background color.

Architecturally, that matters for two reasons:

- rendering is not limited to immutable sprite layers
- script-driven image operations can affect later visual output without round-tripping through authored cast data

## 8. Hit Testing Relationship

Rendering also feeds input. `StageRenderer` publishes baked sprite information so that later hit testing can inspect the most recent sprite order and geometry.

The hit-testing model is currently:

- walk front-to-back using the last baked sprites
- prefer Director-style bounds tests by default
- use per-pixel alpha testing only for true native-alpha content where the implementation can trust the alpha channel

This is a pragmatic compromise between compatibility and runtime cost.

## 9. Compatibility-Sensitive Areas

The most compatibility-sensitive rendering areas in the current codebase are:

- matte and background-transparent handling
- darken/lighten family inks
- script-modified 32-bit bitmaps
- palette invalidation
- text background and anti-alias recovery
- film loop nesting

These are the places where visual regressions are most likely to surface if the emulator diverges from Director's historical assumptions.

## 10. Practical Summary

The rendering system is not "draw whatever the score says". It is:

- a runtime sprite registry
- a bake stage that materializes member content under Director rules
- a software compositor with compatibility-specific ink logic
- a snapshot generator that both display and hit testing can consume

That design is one of the stronger parts of the emulator because it localizes complex compatibility rules into a few clear stages.

## Confidence Score

- Rendering pipeline structure: `9.3/10`
- Ink and compositing model: `8.9/10`
- Text, shape, and film-loop handling: `8.7/10`

Reason for score: the main render flow is directly visible in `FrameRenderPipeline`, `StageRenderer`, `SpriteBaker`, `BitmapCache`, and `SoftwareFrameRenderer`. Confidence is slightly lower in compatibility-heavy visual edge cases because those depend on many format variations and movie-specific authoring tricks.
