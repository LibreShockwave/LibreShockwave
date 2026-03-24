# Theatredrome Room Wrong Colors

## Problem
The Theatredrome public room renders with multiple visual issues:
- **Wrong colors everywhere**: Floor/carpet is dark grey/black instead of warm reddish-brown;
  walls are grey instead of warm beige/cream; curtains are dark/muted instead of red;
  overall scene is desaturated/dark instead of warm-toned
- **White box**: A white rectangular area appears in the bottom toolbar/room bar region
  where reference shows a properly dark-themed bar with controls
- **User figure looks messed up**: The avatar/user figure on the right side renders with
  incorrect colors or appearance compared to the reference

## Screenshots
- Wrong: `docs/our-threatre-wrong.png`
- Reference: `docs/threatre-reference.png`

## Test
Run: `./gradlew :player-core:runTheatredromeTest`
Output: `build/theatredrome/`

## Navigation
SSO login -> Public Spaces tab (421,76) -> Theatredrome row (657,157) -> Room loads automatically.
External cast: `hh_room_theater.cct` loaded from sandbox.h4bbo.net.

## Root Cause Investigation

### Key Findings
1. **Main background sprite**: `teatteri` (ch=109), 688x474, 8-bit, paletteId=25
2. **Palette resolves successfully** from external cast file (hh_room_theater.cct)
3. **Palette colors are wrong**: First 16 colors are greys/blue-greys (0x4B5055, 0x3A4B58, etc.)
   instead of warm browns/reds expected for the Theatredrome
4. **Movie palette** is System Mac (0xFFFFFF, 0x00FFFF, 0xFF00FF...) - NOT the room palette

### Suspected Bug: `clutCastLib` field is ignored
In `BitmapInfo.java` (line 122), the `clutCastLib` field is read but **skipped**:
```java
// clutCastLib (skip)
if (reader.bytesLeft() >= 2) reader.skip(2);
```

The `clutCastLib` field specifies WHICH cast library contains the palette. When a bitmap
in an external cast (like hh_room_theater.cct) has `clutCastLib` pointing to a different
cast library (e.g., the main movie), we should resolve the palette from that library,
not from the bitmap's own file.

ScummVM reads this field and uses it for palette resolution:
- `_clut` stores both `clutCastLib` and `clutId` as a `CastMemberID`
- If `clutId <= 0`, it's a builtin palette (System Mac, Rainbow, etc.)
- If `clutId > 0`, it's a custom palette in the specified cast library

### How ScummVM handles room palettes
ScummVM dithers all 8-bit bitmaps to match the **score palette** (movie palette):
1. Score/frame has a palette channel -> sets the active movie palette
2. Each bitmap has its own `_clut` -> source palette
3. If they differ, bitmap pixels are remapped from source to target palette
4. Room loading typically changes the movie palette via Lingo (`puppetPalette`)

### Files Involved
- `sdk/src/main/java/com/libreshockwave/cast/BitmapInfo.java` - clutCastLib skipped
- `sdk/src/main/java/com/libreshockwave/lookup/PaletteResolver.java` - resolves palette
- `player-core/src/main/java/com/libreshockwave/player/BitmapResolver.java` - cross-file resolution
- `player-core/src/main/java/com/libreshockwave/player/render/pipeline/BitmapCache.java` - caching
- `player-core/src/main/java/com/libreshockwave/player/render/pipeline/SpriteBaker.java` - baking

### Palette Data from Test
```
Palette 25 (from hh_room_theater.cct) first 16:
0xFFFFFF 0x000000 0x5B0205 0x4B5055 0x3A4B58 0x3D4347 0x4B5257 0x425663
0x474F54 0x495156 0x8B969D 0x33444E 0xA0ACB3 0x4A6E80 0x3D5A69 0x3D474C

Movie palette first 16 (System Mac):
0xFFFFFF 0x00FFFF 0xFF00FF 0x0000FF 0xFFFF00 0x00FF00 0xFF0000 0x808080
0xA0A0A4 0xFFFBF0 0x333333 0x996600 0x336633 0x003399 0xCC00FF 0x880000
```

## Next Steps
1. Parse clutCastLib in BitmapInfo instead of skipping
2. Use clutCastLib for cross-cast palette resolution
3. Investigate if room palette switching via Lingo (`puppetPalette`) is needed
4. Check if the palette itself is being read correctly (CLUT chunk parsing)
