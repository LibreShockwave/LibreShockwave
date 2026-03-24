# Private Room Wall Rendering Investigation

## Status
Floor renders correctly. Walls missing.

## Committed Fixes
1. **`01d1a78`** - Fix private room rendering black + test
   - DARKEN ink `skipGraduatedAlpha` fix in `InkProcessor.java:114` (primary floor fix)
   - `skipBgTransparent` removal in `SpriteBaker.java:201-206`
   - `setStageProp("bgcolor")` datum type handling in `MovieProperties.java:336`
   - New `PrivateRoomEntryTest` + Gradle task

2. **`dc7ebea`** - Add `min()` and `max()` Lingo builtins in `MathBuiltins.java`
   - Required by room Visualizer's `updateBounds` during `buildVisual`

## Progress
- Room went from 97.4% black to 32.5% black
- Floor, door, avatar, room info bar, bottom UI all render correctly matching reference
- Reference image: `C:/Users/alexm/Documents/ShareX/Screenshots/2026-03/room-private-reference.png`

## Remaining Issue: Missing Walls

### What we know
- Wall panel bitmaps (311x162, 311x205, 311x216, etc.) ARE being created by the Visualizer
- These panels have tiled wall patterns drawn via copyPixels
- But they are never:
  - Assigned to visible sprite channels
  - Composed (copyPixels'd) into the 720x540 room bitmap (Ch0)
  - Drawn to the stageImage (which remains all black)
- The stageImage is never written to by Lingo code at all
- The 720x540 room bitmap (Ch0) only receives floor tile copyPixels (~100 calls)

### Sprite layout in room
- Ch0: 720x540 DARKEN ink bg=0x996600 - room floor bitmap (floor tiles drawn, no walls)
- Ch112+: door, avatar, UI bar, room info, chat controls
- No wall sprites exist in the channel list

### Key errors during room entry
- "Sprite not marked as usable: 0" - from Lingo SpriteManager (setEventBroker)
- "Listener not found: 370/info, 361/info"
- "User object not found: 14437"

### Hypothesis
The Visualizer creates wall panels but the sprite allocation step fails silently.
The wall panels should be assigned to sprite channels via `pSpriteManager.getFreeSpr()`
but either the free sprite pool is empty or the assignment code has an error.

### Files to investigate
- Room Visualizer Lingo code in `hh_room.cct` / `hh_room_private.cct`
- SpriteManager Lingo code in `fuse_client.cst`
- `player-core/.../SpriteProperties.java` - sprite property setter
- `player-core/.../render/pipeline/StageRenderer.java` - sprite registry
