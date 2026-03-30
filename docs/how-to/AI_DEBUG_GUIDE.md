# AI Debug Guide For Visual Rendering Issues

This guide is for any AI assistant working on LibreShockwave rendering bugs. It captures a repeatable debugging workflow, the main places to inspect, and real examples that proved useful.

The goal is not to guess. The goal is to measure, isolate, trace, and verify.

## Core Rules

1. Start with a reproducible test and save baseline output.
2. Compare our output to a trusted reference image before changing code.
3. Add narrow, disposable debug logging or dumps only where needed.
4. Trace the exact bitmap or text member that produces the bad pixels.
5. Prefer generic pipeline fixes over sprite-specific or movie-specific patches.
6. Remove temporary diagnostics before committing.
7. Re-run the visual test after every meaningful change.

## Primary Test

Use the navigator screenshot test:

```bash
./gradlew :player-core:runNavigatorSSOTest
```

Artifacts are written to:

```text
player-core/build/navigator-sso/
```

Most useful files:

- `02_our_output.png`
- `05a_nav_region_ours.png`
- `05b_nav_region_ref.png`
- `05_nav_region_diff.png`
- `06_nav_side_by_side.png`
- `sprite_info.txt`

Typical metric line:

```text
Total: 388800 | Identical: 275903 (71.0%) | Close: 8104 (2.1%) | Different: 104793 (27.0%)
```

Record the baseline before changing anything.

## Debugging Workflow

### 1. Identify the symptom precisely

Do not accept vague reports like "it looks wrong". Restate the failure in pixel terms.

Examples:

- missing body text
- wrong position
- grey halo around anti-aliased text
- opaque background where the reference shows transparency
- underline missing

If the user gives a specific RGB value, treat that as a strong clue.

### 2. Compare ours vs reference

Read:

- `05a_nav_region_ours.png`
- `05b_nav_region_ref.png`
- `06_nav_side_by_side.png`

Decide which class of failure it is:

- missing element
- wrong color
- wrong alpha/transparency
- wrong position
- wrong size
- wrong compositing order

### 3. Use `sprite_info.txt`

This file is often the fastest way to narrow the problem to a specific sprite or dynamic bitmap.

Look for:

- channel
- type
- position
- ink
- blend
- baked size
- whether it is dynamic

Patterns that often matter:

- `MATTE`
- `BACKGROUND_TRANSPARENT`
- dynamic bitmaps with suspicious dimensions
- text sprites vs bitmap sprites

### 4. Trace upstream, not just final compositing

The rendering stack is usually:

```text
Lingo script
  -> member/image/text mutation
  -> image.copyPixels / fill / draw
  -> runtime bitmap
  -> SpriteBaker
  -> ink processing
  -> final frame compositing
```

If the wrong pixels already exist in an intermediate bitmap, fixing the final renderer is the wrong layer.

### 5. Add narrow diagnostics

Use `System.err` for temporary logging from JVM tests.

Good places:

- `CastMember.renderTextToImage(...)`
- `ImageMethodDispatcher.copyPixels(...)`
- `SpriteBaker`
- `BitmapCache.getProcessedDynamic(...)`
- ink processors

Only log for the exact dimensions or conditions you care about. Example:

```java
if (dest.getWidth() == 311 && dest.getHeight() == 162) {
    System.err.printf("[DEBUG-CP] srcBmp=%dx%d srcRect=%s destRect=%s ink=%s%n",
            src.getWidth(), src.getHeight(), srcRect, destRect, ink);
}
```

If needed, dump the bitmap to PNG and inspect its raw colors.

### 6. Prefer counting pixels over eyeballing

For color/alpha issues, inspect the actual bitmap contents.

Useful questions:

- Is `RGB 221,221,221` present at all?
- Is it opaque or already transparent?
- Is the bad color in the source bitmap or only after compositing?
- Does the label bitmap itself contain the background, or only the row strip behind it?

Simple scripts using Python or PowerShell pixel counts are often enough.

### 7. Verify the fix at the right layer

After a candidate fix:

1. run focused unit tests if possible
2. run `runNavigatorSSOTest`
3. inspect side-by-side output again
4. confirm the problematic color/alpha state is gone
5. confirm global metrics did not regress badly

### 8. Remove temporary diagnostics

Before finishing:

- remove conditional debug logging
- remove temporary PNG dumps
- remove one-off comments added only for the investigation

## Common Root Causes

### Color arithmetic on packed integers

Director's `Color` type supports per-channel ADD/SUB. If the VM's arithmetic opcodes fall through to integer math on packed RGB, byte underflow/overflow corrupts adjacent channels. Always handle `Color +/- Color` with per-channel clamping.

### SpriteRef type conversion

`SpriteRef.toInt()` must return the channel number. The `default -> 0` case in `Datum.toInt()` silently returns 0 for unhandled types. Any new Datum type that can be used in integer context needs an explicit `toInt()` case.

### 32-bit image initialization

`image(w, h, 32)` must create transparent (alpha=0) canvases. Opaque white init breaks DARKEN/LIGHTEN ink processing — matte removes the white content along with the white background.

### DARKEN ink pipeline

DARKEN ink has three steps: (1) matte/background-transparent, (2) multiply by bgColor, (3) alpha composite. Common bugs:
- Skipping `skipGraduatedAlpha` for script-modified bitmaps
- `resolveBackColor` returning -1 for useAlpha=true bitmaps, preventing the multiply
- The matte step removing wall/panel content that should survive (use native alpha detection)

### Text height and auto-size

Director text with `boxType = 0` is auto-sized. If code uses the stored rect height instead of actual content height, later layout can place content far below the visible area.

### Grayscale `copyPixels` remap

Director often builds UI by copying grayscale source images with `#color` and `#bgColor`. If this remap is applied blindly, it can:

- flatten already-colored images
- fill transparent text backgrounds with an opaque color
- destroy intended alpha masks

### Matte alpha handling

`MATTE` should remove border-connected matte/background pixels, but it must preserve existing source alpha. If code forces all surviving pixels to opaque, transparent fringe/background pixels become visible blocks.

### Background transparent alpha recovery

`BACKGROUND_TRANSPARENT` on 32-bit anti-aliased buffers often needs alpha recovery from RGB values blended against the background color. Exact color-keying alone is not enough.

## Worked Example 1: Missing Habborella Body Text

### Symptom

The title rendered, but the body text below it was missing.

### Investigation

1. Pixel comparison showed the body text existed in the reference but not in our output.
2. `CastMember.renderTextToImage()` logging showed:
   - title width `163`, height `480`
   - body width `230`, height `0`
3. `ImageMethodDispatcher.copyPixels()` logging showed a composite image where the body text started hundreds of pixels below the top, but the caller only read the top `56` pixels.

### Root cause

The title member had `boxType = 0` but was rendered with a stale stored height instead of auto-sized height. That pushed the body text far down in the composite.

### Fix

For `boxType = 0`, render with `height = 0` so the text renderer auto-sizes to content.

### Lesson

If text is missing, always inspect actual text bitmap dimensions before touching compositing code.

## Worked Example 2: Navigator `Open` Grey Background

### Symptom

The `Open` label had a slightly lighter grey block behind it that should not exist. The user specifically called out `RGB 221,221,221` and later clarified there should be no background behind `Open` at all.

### What did not work

- sprite-specific cleanup
- stage-specific cleanup
- final-frame postprocessing
- assuming the text glyph bitmap itself was bad

Those approaches were rejected or incorrect because the bug needed a generic pipeline fix.

### Investigation path

1. Use `sprite_info.txt` to narrow the area:
   - the visible row content came from dynamic `MATTE`/`BACKGROUND_TRANSPARENT` bitmaps
   - important intermediate sizes were `311x205`, `311x162`, `311x16`, and `33x10`
2. Add conditional logging in `ImageMethodDispatcher.copyPixels()` for those dimensions.
3. Dump and inspect the intermediate bitmaps.
4. Count actual pixel colors and alpha values instead of relying on visual guesses.

### Key findings

#### Finding 1: the `33x10` `Open` bitmap was clean

It was just black glyph pixels on a transparent background.

So the grey box was not in the text source itself.

#### Finding 2: the row builder copied `Open` with:

- `ink = BACKGROUND_TRANSPARENT`
- `bgColor = 0xDDDDDD`

That was the critical clue.

#### Finding 3: our grayscale remap logic in `ImageMethodDispatcher.copyPixels()` treated that as a normal foreground/background remap

That converted a transparent text background into an actual grey-filled bitmap before applying the ink.

In other words, we were manufacturing the grey panel ourselves.

#### Finding 4: `MATTE` handling also had a generic alpha bug

Both matte paths were forcing surviving pixels to opaque:

```java
result[i] = pixels[i] | 0xFF000000;
```

That turns already-transparent or semi-transparent pixels into visible artifacts.

### Generic fixes that worked

#### Fix A: preserve alpha in matte processing

Applied in:

- `player-core/.../InkProcessor.java`
- `sdk/.../Drawing.java`

Border-connected matte pixels still become transparent, but surviving pixels keep their original alpha instead of being forced opaque.

#### Fix B: preserve transparent grayscale text backgrounds during `copyPixels`

Applied in:

- `vm/.../ImageMethodDispatcher.java`

When all of these are true:

- source is grayscale
- source already contains transparency
- `ink == BACKGROUND_TRANSPARENT`
- `#bgColor` is provided
- no `#color` remap is provided

then treat the source as an alpha mask instead of remapping transparent background into an opaque `bgColor` fill.

### Result

After the fix:

- opaque `221`, `222`, `238`, and `239` greys were gone from the navigator crop
- navigator SSO metrics improved materially
- the fix was generic and not tied to navigator-specific channels or coordinates

### Lesson

If a label appears to have a colored box behind it, verify whether:

1. the label bitmap already contains the box
2. the row strip behind it contains the box
3. `copyPixels` remap is fabricating the box from `#bgColor`
4. a later matte step is making transparent pixels visible

Do not assume the visible artifact belongs to the text bitmap itself.

## Worked Example 3: Private Room Renders Black (Floor + Walls)

### Symptom

Entering a private room produces an almost fully black screen (97.4% black pixels) instead of showing yellow walls, brown floor, door, window, and avatar.

### Investigation (multi-layered — each fix revealed the next issue)

#### Layer 1: Floor was black

`sprite_info.txt` showed Ch0 (720x540, DARKEN ink, bg=0x996600) as the room bitmap. The raw bitmap had floor tile outlines on white. DARKEN ink's matte removed white, then should multiply remaining by bgColor. But the graduated alpha unblending in `applyBackgroundTransparent` was incorrectly making floor pixels semi-transparent.

**Root cause:** `InkProcessor.applyInk` DARKEN branch called `applyBackgroundTransparent(src, matteColor)` without passing `skipGraduatedAlpha`. Script-modified room bitmaps need exact color-key only.

**Fix:** Pass `skipGraduatedAlpha` to the DARKEN/LIGHTEN branch's `applyBackgroundTransparent` call.

#### Layer 2: Walls missing entirely

After fixing the floor, walls were still absent. Tracing showed:
1. Wall panel bitmaps WERE created (100+ `copyPixels` calls on 311x162 etc. sized bitmaps)
2. The Visualizer allocated sprites 110-117 for walls
3. But all wall Part Wrappers had `pSprite = sprite(0)` instead of their allocated channels
4. Wall rendering went to sprite(0) (the floor) instead of the wall sprites

**Root cause:** `SpriteRef.toInt()` returned 0 for ALL SpriteRef types (the `default -> 0` case in Datum.toInt). When Lingo did `sprite(spriteRef)`, it called `toInt()` on the SpriteRef argument, producing sprite(0).

**Fix:** Add `case SpriteRef sr -> sr.channelNum()` to `Datum.toInt()`. This is a generic VM fix — any Director movie that passes sprite references through integer conversion was broken.

#### Layer 3: Wall bitmaps were transparent (not colored)

Walls appeared but were white/uncolored. The `image(w, h, 32)` builtin created canvases with opaque white (0xFFFFFFFF). Wall content drawn via copyPixels was also white. DARKEN's matte removed all white.

**Root cause:** Director's `image(w, h, 32)` creates transparent canvases (alpha=0), not opaque white. The transparent background lets DARKEN skip matte (via native alpha detection) and multiply wall content by bgColor to produce the wall color.

**Fix:** Initialize 32-bit `image()` bitmaps with `0x00FFFFFF` (transparent white). Detect native alpha in SpriteBaker and pass `useAlpha=true` so DARKEN skips matte. Ensure DARKEN always resolves the bgColor tint regardless of useAlpha.

#### Layer 4: Left wall was pink instead of yellow

Left wall rendered as pink (0xEFBBF0), right wall correct yellow (0xFFCC00). Bytecode analysis of `Private_Room_Engine_Class.ls` (found in extracted Director assets at `C:\Users\alexm\Documents\director_assets\14.1_b8\hh_room_private\`) revealed the Lingo computes: `leftColor = baseColor - rgb(16,16,16)`.

**Root cause:** The SUB opcode had no Color type handling. `Color(255,204,0) - Color(16,16,16)` fell through to integer subtraction on packed RGB: `0xFFCC00 - 0x101010 = 0xEFBBF0`. The blue channel `0x00 - 0x10` underflowed to `0xF0`, corrupting the green channel via borrow.

**Fix:** Add per-channel Color arithmetic with clamping to ADD and SUB opcodes: `Color(max(0, r1-r2), max(0, g1-g2), max(0, b1-b2))`. Generic fix for all Director movies using Color math.

### Key Lessons

1. **Multi-layered bugs**: fixing one rendering issue often reveals the next. Each layer had a DIFFERENT generic root cause. Don't stop after the first fix.

2. **Always check extracted Director assets** (`C:\Users\alexm\Documents\director_assets\14.1_b8\`). The `.ls` Lingo bytecode files show exactly what the scripts do. This is how we found the `Color - Color` subtraction that produced the pink wall.

3. **NEVER make movie-specific fixes.** Every fix must be a generic Director engine improvement. Referencing specific property names like "pSprite", "updateWrap", "Room_visualizer" is NOT allowed. Find the VM/rendering engine bug that causes the symptom.

4. **`SpriteRef.toInt()` returning 0 was the root cause of sprite assignment failures.** Any time sprite references pass through integer conversion (constructors, arithmetic, property access), the channel number must be preserved.

5. **Color arithmetic must be per-channel.** Director's Color type is NOT a packed integer for arithmetic purposes. ADD/SUB/MUL on Colors must operate channel-by-channel with clamping.

6. **32-bit `image()` canvases must be transparent.** Director initializes 32-bit images with transparent white (alpha=0). Opaque white breaks ink processing (DARKEN matte removes the content instead of the background).

7. **Trace the Lingo call stack** when a sprite property has a wrong value. The value may come from score data, from `applyScoreDefaults`, from Lingo's `SET` opcode (property by ID), or from `SET_OBJ_PROP` (property by name). These are DIFFERENT code paths.

## Worked Example 4: Oasis Spa Water Animation Uses The Wrong Bitmap

### Symptom

In `Lounges and Clubs > Oasis Spa`, the pool water shows the authored placeholder pattern instead of the live teal ripple buffer that the room script draws every frame.

### Investigation

The extracted room scripts for `hh_room_pool` showed that `pool_b Class` creates `Water Ripple Effects Class` and initializes it with `vesi2`. That helper does not swap the sprite member. Instead, it does:

1. `tSpr = ...getSprById("vesi2")`
2. `pMemberImg = tSpr.member.image`
3. `pMemberImg.fill(..., rgb(0, 153, 153))`
4. `pMemberImg.draw(...)` for each ripple oval

That means the authored score sprite stays bound to the same member slot, but the runtime `CastMember` wrapper owns a mutated live bitmap buffer.

### Root Cause

`SpriteBaker.bakeBitmap()` only used the runtime member path for explicitly dynamic sprites. For a score-placed bitmap sprite, it baked from the authored `CastMemberChunk` unless the sprite itself had a dynamic member override. Oasis Spa water is not a dynamic sprite override; it is an authored score sprite whose `member.image` is mutated at runtime.

The follow-up failure was that score rendering resolves authored members through `DirectorFile`, while runtime wrappers live under `CastLib`. Our lookup helper only matched `CastMemberChunk` by Java object identity, so the score sprite's authored chunk could fail to resolve back to its runtime wrapper even though both represented the same cast slot.

So the renderer kept decoding the original cast asset for `vesi2` instead of using the runtime member's script-modified bitmap.

### Fix

Resolve the runtime `CastMember` wrapper for score-backed bitmap sprites before falling back to the authored `CastMemberChunk`. Match authored members by stable cast identity (`source file + chunk id`), not by object identity. If that runtime member's bitmap is script-modified, bake from the live bitmap buffer and then apply the sprite's ink/blend rules.

This is a generic rendering rule, not an Oasis Spa special case:

- authored score sprites can still depend on runtime member state
- `member.image` mutation does not imply a dynamic sprite binding
- the bake stage must choose between authored media and live runtime media

### Validation

Add a regression that:

1. loads `hh_room_pool.cst`
2. mutates `vesi2` through the runtime `CastMember`
3. renders it through the normal score-sprite `CastMemberChunk` path
4. asserts the baked output matches the live `MASK`-ink water buffer

Run:

```bash
./gradlew :player-core:test --tests "com.libreshockwave.player.render.pipeline.PublicRoomInkRegressionTest"
./gradlew :player-core:test --tests "com.libreshockwave.player.ScriptModifiedBitmapTest"
```

## Useful Commands

Run visual tests:

```bash
./gradlew :player-core:runNavigatorSSOTest
./gradlew :player-core:runNavigatorClickTest
./gradlew :player-core:runPrivateRoomEntryTest
./gradlew :player-core:runPurseTest
```

Run focused tests:

```bash
./gradlew :player-core:test --tests "com.libreshockwave.player.ScriptModifiedBitmapTest"
./gradlew :player-core:test --tests "com.libreshockwave.player.render.pipeline.InkProcessorTest"
./gradlew :sdk:test --tests "com.libreshockwave.bitmap.DrawingMatteTest"
```

## Extracted Director Assets

Decompiled/extracted assets from the Habbo DCR and external casts are at:

```
C:\Users\alexm\Documents\director_assets\14.1_b8\
```

Each subfolder (e.g. `hh_room_private/`, `fuse_client/`) contains:
- `.ls` files: Lingo bytecode disassembly (shows exact opcodes and handler logic)
- `.png` files: extracted bitmap members
- `.pal` files: palette data (JASC-PAL text format)
- `.txt` files: text member content

**When debugging Lingo behavior, always check the `.ls` bytecode first.** It shows the exact opcode sequence, handler names, and string constants. This is far more reliable than guessing what the Lingo code does.

Search for important rendering code:

```bash
rg -n "copyPixels|renderTextToImage|BACKGROUND_TRANSPARENT|MATTE|InkProcessor|SpriteBaker" player-core vm sdk
```

## Reference Documentation

Director references in `/docs/` are authoritative when behavior is unclear:

- `drmx2004_scripting_ref.pdf`
- `drmx2004_getting_started.pdf`

Consult them when deciding how inks, text properties, `copyPixels`, `member.image`, or sprite behavior should work.
