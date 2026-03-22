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

### Bug 2: Floor tile click does not trigger walking

| Detail | Value |
|---|---|
| Click point | (341, 272) |
| Hit channel | 109 (interactive) |
| Sprite hit | `background` (single 714x415 bitmap at (3,41)) |
| Screen change | 2.59% (minor rendering variation, avatar does not move) |

The click hits the `background` sprite — a single large bitmap covering the entire room floor area. This is the room's pre-rendered background, not an individual floor tile. The Habbo client uses **isometric coordinate mapping** to convert screen pixel coordinates to tile grid positions, then sends a `MOVE` packet to the server with the target tile.

Likely causes:
- The isometric screen-to-tile coordinate conversion may not be implemented (the click is on the `background` bitmap, but the tile coordinate calculation happens in Lingo script, not via sprite hit testing)
- Even if tile coordinates are calculated, the walk command requires a server roundtrip — the client sends `MOVE` and the server responds with pathfinding updates
- The `eventProcUserObj: User object not found: 47667` error during room load suggests the user object wasn't properly registered, which would prevent movement commands

### Errors during room interaction

```
Listener not found: 370 / info      — room message handler missing
Listener not found: 361 / info      — room message handler missing
User object not found: 47667        — avatar object not registered
```

The `User object not found: 47667` error is significant — it means the room's user management system doesn't have an object for the logged-in user (ID 47667). This would prevent any user actions (wave, walk, dance) from being processed since the client can't find its own avatar object to apply actions to.

## Root Cause Summary

Both interactions fail because:

1. **Missing user object** — `eventProcUserObj` can't find user 47667, so any action targeting the avatar (wave gesture, walking) has no object to apply to
2. **Server dependency** — Both wave and walk require server roundtrips. The client sends the action, the server validates and responds with the visual update. Without server responses, no animations play.
3. **Floor tile detection** — Walking also requires isometric screen-to-tile coordinate conversion in the Lingo scripts, which may or may not be functioning. The `background` sprite hit is expected (it's the room bitmap), but the Lingo `mouseUp` handler on it needs to calculate which tile was clicked.

## Coordinate Notes

WASM canvas and Java player both render at 720x540 (the Habbo DCR movie's native stage dimensions). Coordinates are 1:1 between the two — no conversion needed. The WASM `getCanvasPoint()` function handles any CSS scaling transparently.
