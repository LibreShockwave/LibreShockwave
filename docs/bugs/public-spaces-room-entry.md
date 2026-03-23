# Public Spaces: Wave and Walk Interactions Non-Functional in Welcome Lounge

## Reproduction Steps

Starting from the SSO navigator test flow (coordinates are in the 720x540 stage space, same for WASM and Java):

1. SSO login, wait for hotel view + navigator to load
2. Click **Public Spaces** tab at (421, 76)
3. Click **Welcome Lounge "Go" button** at (657, 137)
4. Wait for room to load (~600 ticks / 40s) — room loads successfully with 147 sprites
5. Click **Wave button** at (629, 473) — hit lands on `Room_interface_wave.button` (ch 259) but **no wave animation plays**
6. Click **floor tile** at (341, 272) to walk — hit lands on `background` (ch 109) and **Habbo does not walk**

Test: `./gradlew :player-core:runPublicSpacesWalkTest`
Output: `build/public-spaces-walk/`

## Findings

### Room loads and renders correctly

After clicking the row-level "Go" button at (657, 137), the Welcome Lounge room loads with 147 sprites and renders fully:
- Room component state: `pActiveFlag=1`, `pRoomId="welcome_lounge"`
- Process list: `[#passive: 1, #Active: 1, #users: 1, #items: 1, #heightmap: 1]`
- Habbo avatar ("Alex") appears at the bottom right of the room
- Room bar with "Home Page", "Wave", "Dance" buttons renders at bottom
- Room loads additional casts: `hh_room_nlobby.cct`, `hh_people_small_*.cct`, `hh_pets*.cct`, `hh_cat_*.cct`

### Bug 1: Wave button click has no visible effect

| Detail | Value |
|---|---|
| Click point | (629, 473) |
| Hit channel | 259 (interactive) |
| Sprite hit | `Room_interface_wave.button` |
| Screen change | 2.73% (info card disappears, no wave animation) |

The click correctly reaches `Room_interface_wave.button` — the hit test confirms the button sprite is interactive and receives the mouse event. However, **no wave animation plays on the Habbo avatar**. The 2.73% screen change is just the user info card ("Alex / I'm a new user!") disappearing, not a wave gesture.

Likely cause: The wave button's mouseUp handler sends a network message to the game server (e.g., an `WAVE` action packet). The server must echo back the action to trigger the avatar animation via `eventProcUserObj`. Without a server-side response, the client-side handler executes but produces no visible change.

### Bug 2: Floor tile click does not trigger MOVE to server

| Detail | Value |
|---|---|
| Click point | (341, 272) — WASM equivalent: (356, 265) |
| Hit channel | 109 (interactive) |
| Sprite hit | `background` (single 714x415 bitmap at (3,41)) |
| Expected | `MOVE` message sent to server at localhost:30087 |
| Actual | `eventProcRoom` runs but exits early — `me.getComponent()` returns VOID |

### Errors during room interaction

```
Listener not found: 370 / info      — room message handler missing
Listener not found: 361 / info      — room message handler missing
```

## Root Cause Analysis (traced in detail)

### Full event chain from mouseDown to MOVE

1. **InputHandler** → queues MOUSE_DOWN → `hitTestAll(341,272)` returns `[109]` → `dispatchSpriteEvent(109, "mouseDown")`
2. **EventDispatcher** → finds Event_Broker_Behavior in ch=109's scriptInstanceList → `AncestorChainWalker.hasHandler` finds `on mouseDown` → calls it
3. **Event_Broker_Behavior.mouseDown** → checks `not voidp(pProcList)` → calls `me.redirectEvent(#mouseDown)`
4. **redirectEvent** → gets `pProcList[#mouseDown]` → `[#eventProcRoom, #room_interface]` → calls `objectExists(#room_interface)` → if true, calls `call(#eventProcRoom, getObject(#room_interface), #mouseDown, me.id)`
5. **Room_Interface_Class.eventProcRoom** → calls `me.getComponent().getSpectatorMode()` and `me.getComponent().getOwnUser()` → if ownUser == 0, **early return** (no MOVE sent)
6. **(if getComponent worked)** → checks `pClickAction == "moveHuman"` → calls `me.getGeometry().getWorldCoordinate(the mouseH, the mouseV)` → calls `me.getComponent().getRoomConnection().send("MOVE", [#short: tileX, #short: tileY])`

### Fixed: Bug A — `call()` doesn't dispatch to `SpriteRef` targets

**File:** `vm/.../builtin/flow/ControlFlowBuiltins.java` — `callOnTarget()`

The Room_Interface_Class registers its `eventProcRoom` handler on floor sprites via:
```
call(#registerProcedure, [sprite(109), sprite(110), ...], #eventProcRoom, me.getID(), #mouseDown)
```

The `call()` builtin iterates the sprite list and calls `callOnTarget()` for each `sprite(N)`. But `callOnTarget()` only handled `ScriptInstance` and integer targets. `Datum.SpriteRef.toInt()` returns 0 (via default `toDouble() → 0.0`), so `channel > 0` was false and the call silently did nothing. **`registerProcedure` never reached the Event_Broker_Behavior on floor sprites**, leaving pProcList at template values `[#null, 0]`.

**Fix:** Added explicit `Datum.SpriteRef` handling to extract `channelNum()` directly:
```java
if (target instanceof Datum.SpriteRef sr) {
    channel = sr.channelNum();
} else {
    channel = target.toInt();
}
```

### Fixed: Bug B — PropList cross-type key matching creates duplicate entries

**File:** `vm/.../datum/Datum.java` — `PropList.get(key, isSymbolKey)` and `PropList.put(key, isSymbolKey, value)`

The FUSE framework's `buildThreadObj` registers objects with **symbol** keys (`#room_interface`), but the Window_Manager later re-registers with **string** keys (`"Room_interface"`). The cross-type fallback in both `get()` and `put()` required **exact-case** matching for cross-type lookups. Since `"room_interface"` (symbol name, lowercase) ≠ `"Room_interface"` (string, mixed case), the PropList created **duplicate entries** instead of updating the existing one. This caused `getObject(#room_interface)` to find stale/finalized entries while the valid instance was stored under a different key type.

**Fix:** Made cross-type fallback **case-insensitive** in both `get()` and `put()`, matching Director's case-insensitive PropList behavior.

### Remaining: Bug C — `getComponent()` returns VOID on recreated room interface

**Status:** Not yet fixed. This is the remaining blocker for MOVE.

**Symptom:** `eventProcRoom` is called on the correct instance, but `me.getComponent()` returns VOID. The handler then checks `me.getComponent().getOwnUser() == 0` → `VOID == 0` → true (via `lingoEquals`) → **early return**, no MOVE sent.

**Root cause chain:**

1. During startup, `Thread_Manager.create` calls `buildThreadObj(#room_interface, ["Room Interface Class"], threadObj)`. This creates an ancestor chain: **Room_Interface_Class → Object_Base_Class → Thread_Instance_Class**. The Thread_Instance_Class ancestor has the `component` property. The `getComponent()` handler (on Thread_Instance_Class) does `return me.component`, which walks the ancestor chain and finds it.

2. The room_interface object gets **prematurely finalized** — `pObjectList.setAt(#room_interface, #objectFinalized)` is called during `Buffer_Component_Class.construct()` (a completely different thread). This happens via the FUSE message system: `registerMessage(#objectFinalized, ...)` triggers message delivery that cascades into the room object's finalization.

3. Later, the Window_Manager creates a **new** room interface window via `Object_Manager.create("Room_interface", pInstanceClass)`. Since the finalized entry has `objectp(#objectFinalized) == false`, the "already exists" check passes and a new chain is created: **Room_Interface_Class → Object_Base_Class** — **without Thread_Instance_Class as ancestor**.

4. `getComponent()` on this new instance walks its ancestor chain but never finds Thread_Instance_Class, so `me.component` is VOID.

**Key observations from tracing:**
- `getThread(#room)` returns instance 216 which **does** have `component` set correctly (instance 252)
- `getObject(#room_interface)` returns instance 1841 which does **not** have `component`
- These are **different** instances — the Thread knows about the component, but the Interface doesn't
- The finalization (`#objectFinalized`) is set during the `buildThreadObj` loop for `#buffer_component`, NOT during room destruction
- The PropList ends up with: `setAt(#room_interface, id=217)` → `setAt(id=218)` → `setAt(#objectFinalized)` → `setAt("Room_interface", id=1840)` → `setAt(id=1841)`
- The symbol key `#room_interface` (lowercase) and string key `"Room_interface"` (mixed case) were being stored as separate entries before Bug B fix

**Possible fix directions:**
- Investigate why `Buffer_Component_Class.construct()` triggers finalization of `room_interface` — this may be a message system timing issue where `#objectFinalized` messages are delivered during registration
- Make `Object_Manager.create` preserve the Thread_Instance_Class ancestor when recreating a finalized object
- Alternatively, prevent the premature finalization entirely

## Coordinate Notes

WASM canvas and Java player both render at 720x540 (the Habbo DCR movie's native stage dimensions). Coordinates are 1:1 between the two — no conversion needed. The WASM `getCanvasPoint()` function handles any CSS scaling transparently.
