# Handoff: How To Build A Future JS Loader/Test For V31 Native Navigator

## Purpose
Use this document when asking an AI to create a new `.js` loader or test for the v31 native navigator flow.

The goal of that future loader/test is:
- log into the real v31 client through the existing native WASM regression path
- wait long enough for a real user login to finish
- tolerate brief transient connection-error UI
- capture the navigator only after the client is genuinely settled
- compare the navigator crop against the trusted native reference exactly

This document is not a replacement for the actual goal file. It is a practical build brief for a future AI so it does not repeat the earlier mistakes.

Primary goal file:
- [v31-native-navigator-goal.md](/opt/git/LibreShockwave/docs/bugs/v31-native-navigator-goal.md)

## Base Files To Reuse
The future AI should start from the existing native JS regression path, not invent a separate login path.

Primary harness:
- [wasm-native-visual-regression-test.js](/opt/git/LibreShockwave/player-wasm/src/test/js/wasm-native-visual-regression-test.js)

Reference loader/source-of-truth notes:
- [/opt/git/v14_v31_compare/loader.md](/opt/git/v14_v31_compare/loader.md)

Debugging guide:
- [AI_DEBUG_GUIDE.md](/opt/git/LibreShockwave/docs/how-to/AI_DEBUG_GUIDE.md)

## Non-Negotiable Requirements
- Reuse the existing v31 native login regression flow in `wasm-native-visual-regression-test.js`.
- Use the websocket-backed beta client configuration already used for this goal.
- Use `wss`, not `ws`.
- Use the pinned SSO ticket already documented in the goal file.
- Do not create a fake or simplified login path.
- Do not switch to the older Java navigator tests as the main acceptance path.
- Do not patch around runtime bugs in the loader/test.
- Do not add movie-specific hacks or navigator-only production shims.
- Do not use post-render pixel repair, image fudging, or threshold-only acceptance.

## Required Client Configuration
The future JS loader/test should use the same effective beta-client settings as the active goal:

- `betaClientMovieUrl = "https://images.classichabbo.com/dcr/r31_20090312_0433_13751_b40895fb6101dbe96dc7b9d6477eeeb4/habbo.dcr?"`
- `betaClientParams["venus.websocket.mode"] = "wss"`
- `betaClientParams["sw2"] = "connection.info.host=verysecret.classichabbo.com;connection.info.port=30100"`
- `betaClientParams["sw3"] = "connection.mus.host=verysecret.classichabbo.com;connection.mus.port=38201"`
- `betaClientParams["sw8"] = "use.sso.ticket=1;sso.ticket=vibe-sso-admin-504d3ba4-acdb-4436-b67c-d0752f44f767"`

## What The JS Test Must Wait For
The future AI must not treat “navigator became visible” as success.

The test must:
- wait through the real login flow
- wait through post-handshake traffic
- wait until observable login/load/network activity is finished
- wait a bit longer after network-idle so the UI is actually settled
- only then capture the comparison screenshot

Important:
- Do not assume the login flow settles within 2 minutes.
- If the client is still progressing toward the logged-in navigator state, keep waiting.
- A valid attempt can take more than 2 minutes.

## How To Handle `connection_problem_window`
This was a major past failure mode.

Rules:
- Do not abort immediately just because `connection_problem_window` appears.
- A brief flash of `connection_problem_window` is transient noise.
- Ignore brief appearances if the client continues recovering toward the logged-in navigator state.
- Only treat it as a real failure if it persists long enough to show the client is genuinely stuck.
- Do not capture screenshots while that window is visible.
- Do not treat a frame immediately after recovery as settled; require a further settle wait.

In short:
- brief `connection_problem_window` is not fatal
- persistent `connection_problem_window` is fatal

## Readiness Conditions For Capture
The future JS test should capture only when all of these are true:
- the login has actually completed
- the navigator state is present
- observable network activity has gone idle
- the client has survived any brief transient connection-problem flashes
- an extra settle delay has elapsed after network idle
- the frame is stable enough for exact crop comparison

The future AI should explicitly model:
- login complete
- network idle
- extra settle wait
- capture

It should not collapse these into a single “navigator visible” check.

## Navigator Crop To Use
Use the established crop unless there is hard evidence it is wrong.

Required crop:
- `x=350`
- `y=60`
- `width=370`
- `height=440`

Reference source for this crop:
- [NavigatorSSOTest.java](/opt/git/LibreShockwave/player-core/src/test/java/com/libreshockwave/player/NavigatorSSOTest.java)

## Reference Image
Trusted full-frame native reference:
- [/opt/git/v31_room_load/v31_native.png](/opt/git/v31_room_load/v31_native.png)

The loader/test must compare the actual navigator crop against the crop from that reference image exactly.

## Required Saved Artifacts
Any future JS loader/test created for this goal should save:
- full actual frame
- cropped actual navigator image
- cropped reference navigator image
- cropped diff image
- side-by-side comparison
- sprite/window/text diagnostics if the crop does not match

Preferred artifact naming style:
- `02_our_output.png`
- `05a_nav_region_ours.png`
- `05b_nav_region_ref.png`
- `05_nav_region_diff.png`
- `06_nav_side_by_side.png`
- `sprite_info.txt`

## What The Future AI Must Not Do
- Do not stop on the first visible navigator frame.
- Do not hard-fail at 2 minutes if the session is still progressing.
- Do not abort on a brief `connection_problem_window` flash.
- Do not compare while networking is still active.
- Do not compare during recovery from a connection-problem flash.
- Do not loosen exact comparison into a fuzzy-only pass condition.
- Do not damage v1, v14, or v31 room-entry rendering to make navigator pass.

## Runtime Areas Worth Inspecting If The Crop Fails
If the new loader/test proves the crop is still wrong, the future AI should investigate runtime/rendering layers such as:
- text rendering
- `copyPixels`
- `MATTE`
- `BACKGROUND_TRANSPARENT` / `ink 36`
- dynamic bitmap creation
- window/widget composition
- sprite baking
- final compositing

It should classify the failure precisely:
- missing element
- wrong text
- wrong position
- wrong alpha/transparency
- wrong color/remap
- wrong size
- wrong compositing order
- wrong settled state

## Suggested Structure For A New JS Test
If a future AI creates a new JS test or loader helper, the structure should look like this:

1. Start from the existing native regression harness and v31 case setup.
2. Force `wss` transport.
3. Start the real login flow with the pinned SSO ticket.
4. Wait for post-handshake/login progress markers.
5. Ignore brief `connection_problem_window` flashes.
6. Detect real persistent stuck/error state separately from transient flashes.
7. Wait for navigator-present logged-in state.
8. Wait for network idle.
9. Wait an extra settle period after network idle.
10. Capture the full frame.
11. Crop the navigator.
12. Compare against the crop from `/opt/git/v31_room_load/v31_native.png`.
13. Save all required artifacts.
14. Fail only if the final settled crop still does not match or if the client is genuinely stuck.

## Commands The Future AI Should Keep In Mind
Focused v31 native visual run:

```bash
JAVA_HOME=/opt/jdk-21.0.11+10 PATH=/opt/jdk-21.0.11+10/bin:$PATH \
LS_NATIVE_VISUAL_CASES=v31 \
./gradlew :player-wasm:runWasmNativeVisualRegressionTest \
  -PoutputDir=/opt/git/LibreShockwave/build/native-visual-v31-navigator \
  --no-daemon
```

Offline regression gate for v1 and v14:

```bash
JAVA_HOME=/opt/jdk-21.0.11+10 PATH=/opt/jdk-21.0.11+10/bin:$PATH \
LS_NATIVE_VISUAL_CASES=v1,v14 \
./gradlew :player-wasm:runWasmNativeVisualRegressionTest \
  -PoutputDir=/opt/git/LibreShockwave/build/native-visual-v1-v14-regression \
  --no-daemon
```

v31 room-entry non-regression gate:

```bash
JAVA_HOME=/opt/jdk-21.0.11+10 PATH=/opt/jdk-21.0.11+10/bin:$PATH \
LS_V31_ROOM_ENTER_SKIP_LEGACY_VISUALS=1 \
./gradlew :player-wasm:runWasmV31RoomEnterTest \
  -PoutputDir=/opt/git/LibreShockwave/build/roomEnterTestV31-post-navigator-fix \
  --no-daemon
```

## Success Criteria For The Future AI
The future AI’s JS loader/test is only successful if:
- it uses the real v31 native login flow
- it uses `wss`
- it waits long enough for legitimate slow logins
- it ignores brief transient `connection_problem_window` flashes
- it only captures after network idle plus extra settle time
- it saves navigator crop/diff artifacts
- the actual navigator crop matches the reference crop exactly
- v1 and v14 stay green
- v31 room-entry does not regress
- the solution remains a real runtime/rendering fix, not a harness cheat

## Final Instruction To Give A Future AI
If you want to prompt another AI later, say something close to this:

> Create or update a JS test in `player-wasm/src/test/js/wasm-native-visual-regression-test.js` for the v31 native navigator flow. Reuse the existing native login path, force `wss`, do not assume login completes within 2 minutes, ignore brief transient `connection_problem_window` flashes unless they persist, wait for network idle plus an extra settle delay before capture, crop the navigator at `350,60,370,440`, compare exactly against `/opt/git/v31_room_load/v31_native.png`, save full/crop/diff artifacts, and preserve v1/v14 plus v31 room-entry regressions.
