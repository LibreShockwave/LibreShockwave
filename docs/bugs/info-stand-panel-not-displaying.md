# Info Stand Panel Grey Background Not Displaying

**Status: FIXED**

## Summary

After entering the welcome lobby, the grey info panel (RGB 187, 187, 187) in the bottom-right corner did not display. The info stand window was created and text elements rendered, but the grey background panel behind the username was invisible.

## Root Cause

**`Drawing.applyInk()` MATTE ink ignored the `blend` parameter and used inverted blend convention.**

The `info_name_bg` bitmap is an 8-bit black-filled rounded rectangle (paletteId=2, custom 2-color palette: index 0=white, all others=black). White pixels are removed by matte. The remaining black pixels should composite at `#blend: 70` over the white buffer, producing grey ~187.

Two issues:
1. **MATTE ignored `blend`** — used only `srcA` (255), so black overwrote white at 100% → pure black instead of grey.
2. **Blend direction inverted** — Director's internal `blendAmount` convention: 0=fully visible, 255=invisible. ScummVM confirms at `graphics.cpp:342`: `lerpByte(src, dst, blendAmount, 255)` where `blendAmount = 255 - opacity`. The formula `alphaBlend(src, dst, 255 - blend)` produces: black over white with blendAmount=77 → `255*178/255 = 178` ≈ 187.

## Fix

`sdk/src/main/java/com/libreshockwave/bitmap/Drawing.java` — `applyInk()` MATTE case:
- When `blend < 255` (partial): use `alphaBlend(src, dest, 255 - blend)` (Director's inverted convention)
- When `blend == 255` (full opacity): use `alphaBlend(src, dest, srcA)` (normal matte behavior)

The `info_name_bg` element (a white rounded rectangle) is composited into the grouped element buffer via `Grouped_Element_Class.render()`:

```
buffer.image.copyPixels(sourceImage, destRect, srcRect, [#ink: 8, #blend: 70])
```

`Drawing.copyPixels()` (line 55-58) treats `#ink: 8` (MATTE) by calling `applyMatteToRegion()` which flood-fills white from edges making ALL edge-connected white pixels transparent. Since `info_name_bg` is a white rounded rectangle, the flood-fill enters through gaps at corners and removes the entire white interior, leaving only the black outline.

In Director's original `copyPixels`, MATTE ink with `#blend: 70` should composite the white fill at 70% opacity over the destination buffer, producing grey (~187,187,187). The matte should only remove the background outside the shape, not the white fill inside.

### Code Path

1. `Grouped_Element_Class.render()` → `buffer.image.copyPixels(pimage, destRect, srcRect, [#ink: 8, #blend: 70])`
2. `ImageMethodDispatcher.copyPixels()` → `ink = InkMode.MATTE`, `blend = 178` (70% of 255)
3. `Drawing.copyPixels()` line 55: `applyMatteToRegion(src, ...)` → flood-fills white from edges → ALL white pixels become alpha=0
4. `applyInk()` with MATTE: only non-transparent pixels survive → just the thin black outline
5. Result: grey panel invisible

### Fix Location

| File | Line | Issue |
|------|------|-------|
| `sdk/.../bitmap/Drawing.java` | 55-58 | `applyMatteToRegion` flood-fills through corners, destroying interior white fill |

## Confirmed Working Systems

From test output (`NavigatorWelcomeLobbyErrorTest`):

- Window system: `windowExists('RoomBarID') = 1`
- Sprite manager: 832 free of 1000 total
- Info Stand Class loaded: `getmemnum('Info Stand Class') = 1769493`
- Info stand window created (sprites Ch228-Ch231 visible)
- Text elements render correctly

## Info Stand Sprite Layout (from test)

| Channel | Size | Ink | Blend | Role |
|---------|------|-----|-------|------|
| Ch228 | 160x36 @ (553,430) | 36 (BG_TRANSPARENT) | 20 | `bg_darken` |
| Ch229 | 162x55 @ (552,374) | 8 (MATTE) | 100 | `info_stand` grouped (info_plate + info_name_bg) |
| Ch230 | 147x18 @ (559,414) | 36 (BG_TRANSPARENT) | 100 | `info_name` text |
| Ch231 | 157x33 @ (553,431) | 36 (BG_TRANSPARENT) | 100 | `info_text` text |

Ch229 is the grouped wrapper containing both `info_plate` and `info_name_bg`. Individual element blend values (70 for info_name_bg) are applied during copyPixels into the buffer, not at sprite level.

## Trigger Flow

Auto-shows on room entry via `Room_Component_Class.updateProcess()` (line 3896):
```
me.getInterface().getInfoStandObject().showInfostand()
```

## Window Layout Definition

From `hh_room_utils/info_stand.window.txt`:

```
bg_darken:         "info_stand_txt_bg", ink 36, blend 20,  160x36, id "bg_darken"
info_stand (plate): "info_plate",       ink 36, blend 100, 94x50,  id "info_stand"
info_stand (bg):    "info_name_bg",     ink 8,  blend 70,  162x19, id "info_stand"
info_name:         text,                ink 36, blend 100, 147x18, txtColor #EEEEEE
info_text:         text,                ink 36, blend 100, 157x33, txtColor #EEEEEE
```

Elements sharing `#id: "info_stand"` are grouped into `Grouped_Element_Class`, composited into a single buffer via copyPixels with per-element ink/blend.

## Grey Color Derivation

Grey #BBBBBB (187, 187, 187) = white (255) at 70% blend over black (0) buffer: `255 * 0.70 = ~179-187`
