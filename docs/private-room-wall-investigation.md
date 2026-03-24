# Private Room Wall Rendering Investigation

## Status
Floor renders correctly. Walls missing - root cause identified.

## Root Cause
**All wall part wrappers have `pSprite = sprite(0)` instead of their allocated sprites (110-117).**

The Visualizer allocates sprites 110-117 via SpriteManager (confirmed in `pSpriteList`).
But when creating Visualizer Part Wrappers for wall01, wall02, window01, etc., the
`pSprite` property is set to `sprite(0)` instead of the correct sprite reference.

This means wall rendering targets sprite(0) (the floor channel with DARKEN ink)
instead of the dedicated wall sprite channels. The floor bitmap overwrites any
wall content drawn to sprite(0).

### Evidence
```
pSpriteList = [sprite(110), sprite(111), sprite(114), sprite(116), sprite(112), sprite(113), sprite(115), sprite(117)]
wall01.pSprite = sprite(0)    # Should be sprite(110)
wall02.pSprite = sprite(0)    # Should be sprite(111)
window01.pSprite = sprite(0)  # Should be sprite(114)
```

SpriteState for wall channels confirms they never get members:
```
Ch110: member=(0,0) hasDyn=false visible=true puppet=true  # Empty shell
Ch111: member=(0,0) hasDyn=false visible=true puppet=true  # Empty shell
Ch112: member=(12,57) hasDyn=true  # Door - works correctly
```

### Next Steps
The Lingo Visualizer Part Wrapper initialization code sets `pSprite` during part
creation. The sprite reference comes from the Visualizer's sprite list via array
indexing. The indexing or reference creation fails, resulting in sprite(0).

Need to trace how `pSprite` is assigned in the Lingo bytecode to find where
the sprite reference goes wrong.

## Committed Fixes
1. **`01d1a78`** - Fix private room rendering black + test
   - DARKEN ink `skipGraduatedAlpha` fix in `InkProcessor.java:114`
   - `skipBgTransparent` removal in `SpriteBaker.java:201-206`
   - `setStageProp("bgcolor")` datum type handling in `MovieProperties.java:336`
   - New `PrivateRoomEntryTest` + Gradle task

2. **`dc7ebea`** - Add `min()` and `max()` Lingo builtins in `MathBuiltins.java`

3. **`35ad7fa`** - Auto-create bitmap member when sprite.image is set without a member
   - `SpriteProperties.java` image setter enhancement

## Progress
- Room went from 97.4% black to 32.5% black
- Floor, door, avatar, room info bar, bottom UI all render correctly
- Reference image: `C:/Users/alexm/Documents/ShareX/Screenshots/2026-03/room-private-reference.png`
