# Info Stand Panel Not Displaying on Room Entry

## Summary

After entering the welcome lobby, the grey info panel (RGB 187, 187, 187 / #BBBBBB) in the bottom-right corner does not display. In the original client, this panel shows immediately on room entry with the user's name and motto (e.g. "Alex" / "I'm a new user!"), along with "Home Page", "Wave", and "Dance" buttons.

Reference: `docs/chat-message-reference.png`

## Expected Behavior

When the own user enters a room, the info stand panel auto-shows at position [550, 287] with:
- Grey background panel (info_name_bg bitmap, ink 8/matte, blend 70 over the dark room background produces the ~187,187,187 grey)
- Diamond-shaped info plate (info_plate bitmap)
- Dark text background (info_stand_txt_bg bitmap, ink 36/bgTransparent, blend 20)
- Username in white (#EEEEEE) text
- Motto in white (#EEEEEE) text
- Avatar portrait image
- Badge / group badge slots

## Trigger Flow

The info stand display is triggered by the server USERS message:

1. `Room_Handler_Class.handle_users()` (hh_room, line 673) processes the incoming USERS message
2. For each user, checks if the user's name matches the session's own username
3. If so, calls `Room_Interface_Class.eventProcUserObj(#selection, spriteID, #userEnters)` (line 1186)
4. `eventProcUserObj` detects `tParam == #userEnters` (line 4919), sets `pSelectedObj` and `pSelectedType`
5. Calls `Info_Stand_Class.showObjectInfo("user")` (line 4905) via the info stand object

## Window System Chain

The info stand is built on the fuse_client Window system. The creation chain is:

```
Info_Stand_Class.showInfostand()
  -> extCall [createWindow]("info_stand.window", 552, 300)  -- Window_API movie script
    -> getWindowManager().create(...)                        -- Window_Manager_Class
      -> Layout_Parser_Class.parse("info_stand.window")     -- reads text cast member
        -> parse_window() parses the .window.txt content
          -> Sprite_Manager.reserveSprite() for each element
          -> Creates Element_Wrapper_Class instances
          -> Sets sprite member, ink, blend, position, visibility
```

Then `showObjectInfo()` populates the elements:
```
Info_Stand_Class.showObjectInfo(tObjType)
  -> getWindow("info_stand").getElement("bg_darken").show()    -- sets sprite.visible = 1
  -> getElement("info_name").show() + setText(name)
  -> getElement("info_text").show() + setText(motto)
  -> getElement("info_image").resizeTo(w, h) + set member image
```

## Key Files

### Lingo Scripts (director_assets/14.1_b8)

| File | Role |
|------|------|
| `hh_room_utils/Info_Stand_Class.ls` | Info stand controller (showInfostand, showObjectInfo, hideObjectInfo) |
| `hh_room_utils/info_stand.window.txt` | Window layout definition (element positions, members, ink, blend) |
| `hh_room/Room_Interface_Class.ls` | Room event dispatcher (eventProcUserObj triggers info stand) |
| `hh_room/Room_Handler_Class.ls` | Server message handler (handle_users triggers eventProcUserObj) |
| `fuse_client/Window_API.ls` | Movie script providing createWindow, getWindow, windowExists, removeWindow |
| `fuse_client/Window_Manager_Class.ls` | Manages window instances, delegates to Layout_Parser_Class |
| `fuse_client/Window_Instance_Class.ls` | Individual window (getElement, registerClient, lock) |
| `fuse_client/Element_Wrapper_Class.ls` | Window element (show/hide sets sprite.visible, define sets sprite props) |
| `fuse_client/Layout_Parser_Class.ls` | Parses .window.txt text members into element definitions |
| `fuse_client/Sprite_API.ls` / `Sprite_Manager_Class.ls` | Allocates sprites for window elements |

### Bitmap Assets (hh_interface cast)

| Member | Role | Ink | Blend |
|--------|------|-----|-------|
| `info_stand_txt_bg` | Dark bg overlay (black rounded rect) | 36 (bgTransparent) | 20 |
| `info_plate` | Diamond plate shape | 36 (bgTransparent) | 100 |
| `info_name_bg` | Name background (white rounded rect) - produces the grey #BBBBBB | 8 (matte) | 70 |

### Java Implementation

| File | Role |
|------|------|
| `player-core/.../render/pipeline/RenderSprite.java` | Sprite rendering data (visible, ink, blend) |
| `player-core/.../render/output/SoftwareFrameRenderer.java` | Composites sprites with ink/blend |
| `player-core/.../render/pipeline/InkProcessor.java` | Ink mode processing (bgTransparent, matte) |
| `player-core/.../render/pipeline/SpriteBaker.java` | Converts sprites to renderable bitmaps |
| `player-core/.../cast/CastMember.java` | Cast member loading and property access |

## Window Layout Definition

From `info_stand.window.txt`, positioned at rect [550, 287, 719, 453]:

```
bg_darken:    member "info_stand_txt_bg", locH 1, locV 130,   ink 36, blend 20,  160x36,  type "piece"
info_stand:   member "info_plate",        locH 65, locV 74,   ink 36, blend 100, 94x50,   type "piece"
info_stand:   member "info_name_bg",      locH 0, locV 110,   ink 8,  blend 70,  162x19,  type "piece"
info_name:    member "info_name",         locH 7, locV 114,   ink 36, blend 100, 147x18,  type "text", txtColor #EEEEEE
info_text:    member "info_text",         locH 1, locV 131,   ink 36, blend 100, 157x33,  type "text", txtColor #EEEEEE
info_image:   member "shadow.pixel",      locH 111, locV 104, ink 8,  blend 100, 1x1,     type "image" (avatar portrait)
info_badge:   member "shadow.pixel",      locH 129, locV 0,   ink 36, blend 100, 40x40,   type "image"
info_group_badge: member "shadow.pixel",  locH 127, locV 42,  ink 36, blend 100, 40x40,   type "image"
```

## Likely Root Causes

The info stand display depends on the entire fuse_client Window system functioning end-to-end. Potential failure points:

1. **Layout_Parser_Class.parse()** calls `memberExists("info_stand.window")` - if the text member containing the window definition isn't registered with the Resource Manager, parsing silently returns 0 and no window elements are created

2. **Sprite_Manager.reserveSprite()** - window elements need dynamically allocated sprites; if the sprite manager has no free sprites or the allocation fails, elements have no sprite to render on

3. **Element_Wrapper_Class.define()** sets `pSprite` from the allocated sprite and `pBuffer` from the member image - if either is void, `show()` would set visible on a null sprite reference

4. **Member resolution** - the layout parser resolves member names like "info_stand_txt_bg" via the Resource Manager's `getmemnum()`, not the raw cast - if these members aren't in `pAllMemNumList`, they won't resolve

5. **Window_Instance_Class.create()** calls the layout parser, then iterates elements to create Element_Wrapper instances using the configured `pElemClsList` classes (wrapper, unique, grouped) - if any class instantiation fails, the element list stays empty and `getElement("bg_darken")` returns void

## Grey Color Derivation

The grey #BBBBBB (187, 187, 187) comes from the `info_name_bg` bitmap:
- The bitmap is a white rounded rectangle
- Rendered with ink 8 (matte) at blend 70
- White (255) at 70% blend over a black/dark background: `255 * 0.70 + 0 * 0.30 = ~179` to `255 * 0.70 + 30 * 0.30 = ~187`
- This produces the characteristic grey panel behind the username
