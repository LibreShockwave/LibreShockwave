# Goal: V31 loader - native navigator exact render

## Objective
- Main goal: load the v31 client through the existing native login regression flow, stop at the navigator state, and render the navigator correctly.
- Trusted full-frame reference: `/opt/git/v31_room_load/v31_native.png`.
- Primary acceptance target: the cropped navigator region from the actual frame must match the same cropped region from `/opt/git/v31_room_load/v31_native.png` exactly.
- Do not capture the comparison frame while networking is still active. Wait until all observable login/load/network activity has finished, then wait a bit longer so the screenshot is taken from the settled navigator state.
- Treat that extra post-network settle wait as mandatory, even if the navigator is already visible, so comparison is done against a stable final frame rather than an in-flight UI update.
- This is a navigator-rendering goal. Fix the runtime/rendering bug from root cause only.
- No bandaid fixes.
- No navigator-only client shims.
- No post-render pixel repair.
- No test-side image fudging.
- Do not add per-frame special casing in `Player.java`, `StageRenderer`, or WASM harness code just to make the navigator crop pass.

## Active Test
- Primary harness: `player-wasm/src/test/js/wasm-native-visual-regression-test.js`.
- Primary task: `:player-wasm:runWasmNativeVisualRegressionTest`.
- Focused case: `LS_NATIVE_VISUAL_CASES=v31`.
- Use the pinned v31 SSO ticket `vibe-sso-admin-504d3ba4-acdb-4436-b67c-d0752f44f767` for this goal's login flow so navigator investigations run against the intended account/session context.
- Treat the networking path as already provisioned correctly for this goal: websockify is set up, websocket transport is expected to work, and navigator debugging should not assume the remaining mismatch is caused by missing websocket bridging.
- Keep the supplied websocket-backed beta client configuration fixed while working this goal. Do not spend time re-litigating MUS/websocket transport unless there is concrete evidence that the exact supplied `betaClientMovieUrl` / `betaClientParams` are not what produced the trusted reference.
- This is the v31 login/native visual regression path that also supports `v1` and `v14`; reuse it instead of debugging through the older Java `NavigatorSSOTest` path.
- Do not make the legacy Java navigator tests the acceptance gate for this goal. They are useful only as a reference for crop geometry and artifact style.

Recommended focused command:

```bash
JAVA_HOME=/opt/jdk-21.0.11+10 PATH=/opt/jdk-21.0.11+10/bin:$PATH \
LS_NATIVE_VISUAL_CASES=v31 \
./gradlew :player-wasm:runWasmNativeVisualRegressionTest \
  -PoutputDir=/opt/git/LibreShockwave/build/native-visual-v31-navigator \
  --no-daemon
```

## Reference Assets
- Full-frame v31 reference: `/opt/git/v31_room_load/v31_native.png`.
- Offline native comparison directory: `/opt/git/v14_v31_compare/`.
- Existing loader/source-of-truth doc for the native visual regression harness: `/opt/git/v14_v31_compare/loader.md`.
- Debugging workflow: `/opt/git/LibreShockwave/docs/how-to/AI_DEBUG_GUIDE.md`.
- Decompiled/extracted v31 assets: `/opt/git/v31_assets/`.

## Required Navigator Crop And Diff
- Crop the navigator from both actual and reference frames and diff those crops, not just the full frame.
- Default crop box:
  - `x=350`
  - `y=60`
  - `width=370`
  - `height=440`
- This crop is taken from the existing Java navigator diagnostics in `player-core/src/test/java/com/libreshockwave/player/NavigatorSSOTest.java`.
- Keep this crop unless investigation proves the v31 native harness needs a different crop because the actual trusted reference framing differs. Do not change it casually.

Required saved artifacts for each focused navigator investigation run:
- full actual frame
- cropped actual navigator image
- cropped reference navigator image
- cropped navigator diff image
- side-by-side navigator comparison
- any focused sprite/text/window diagnostics needed to explain the bad pixels

Preferred artifact naming is analogous to the existing Java navigator outputs:
- `02_our_output.png`
- `05a_nav_region_ours.png`
- `05b_nav_region_ref.png`
- `05_nav_region_diff.png`
- `06_nav_side_by_side.png`
- `sprite_info.txt`

## Investigation Workflow
- Follow `/opt/git/LibreShockwave/docs/how-to/AI_DEBUG_GUIDE.md`.
- Record baseline metrics before changing code.
- Compare actual vs reference navigator crops before touching runtime code.
- Use pixel counts and region diffs, not eyeballing.
- For screenshot capture, do not stop as soon as the navigator first appears. Wait until all observable networking is done and then allow a short extra settle period before capturing the comparison image.
- The comparison screenshot must come from a network-idle-plus-settle point, not merely the first frame where navigator widgets become visible.
- If the client shows a `Problems Connecting` state during the flow, do not capture there and do not treat that frame as acceptable readiness. The harness must wait past that transient branch and only compare after the client has recovered into the settled navigator state.
- Add narrow temporary diagnostics only where needed.
- Remove temporary diagnostics before finishing.

When the navigator crop is wrong, first classify the failure precisely:
- missing element
- wrong text
- wrong position
- wrong alpha/transparency
- wrong color/remap
- wrong size
- wrong compositing order
- wrong settled state

Preferred runtime layers to inspect:
- text rendering
- `copyPixels`
- `BACKGROUND_TRANSPARENT` / `ink 36`
- `MATTE`
- window/widget composition
- dynamic bitmap creation
- sprite baking and final compositing

## V31 Asset Sources To Use
- Use `/opt/git/v31_assets/` to understand how the navigator is authored and built.
- Start with the navigator Lingo and room UI assets, especially:
  - `/opt/git/v31_assets/projectorrays_lingo/hh_navigator/`
  - `/opt/git/v31_assets/hh_room_ui/text/00614_navigator_popup.window_stxt_0.txt`
  - `/opt/git/v31_assets/projectorrays_lingo/hh_navigator/casts/External/ParentScript 3 - Navigator Window Interface Class.ls`
  - `/opt/git/v31_assets/projectorrays_lingo/hh_navigator/casts/External/ParentScript 4 - Navigator Roomlist Interface Class.ls`
  - `/opt/git/v31_assets/projectorrays_lingo/hh_navigator/casts/External/ParentScript 5 - Navigator Component Class.ls`
  - `/opt/git/v31_assets/projectorrays_lingo/hh_navigator/casts/External/ParentScript 7 - Navigator Info Broker Class.ls`
- Use these assets to determine whether the mismatch comes from authored navigator state, text/window construction, event handling, or render semantics.
- Use the websocket-backed beta client configuration that matches the current environment:
  - `betaClientMovieUrl = "https://images.classichabbo.com/dcr/r31_20090312_0433_13751_b40895fb6101dbe96dc7b9d6477eeeb4/habbo.dcr?"`
  - `betaClientParams["venus.websocket.mode"] = "ws"`
  - `betaClientParams["sw2"] = "connection.info.host=verysecret.classichabbo.com;connection.info.port=30100"`
  - `betaClientParams["sw3"] = "connection.mus.host=verysecret.classichabbo.com;connection.mus.port=38201"`
  - `betaClientParams["sw8"] = "use.sso.ticket=1;sso.ticket=vibe-sso-admin-504d3ba4-acdb-4436-b67c-d0752f44f767"`

## Guardrails
- Preserve normal runtime paths: Director VM semantics, Lingo dispatch, window creation, sprite state, bitmap generation, inks, and compositing.
- Do not hardcode navigator coordinates, colors, strings, or pixel repairs in production code.
- Do not patch the WASM harness to hide a runtime bug.
- Do not replace exact crop comparison with threshold-only acceptance.
- Do not treat the full-frame room-entry target as optional collateral damage.
- Keep the room-entry visual state unchanged while fixing navigator rendering.

## Regression Requirements
- `v1` and `v14` offline native visual regression must stay green.
- The v31 room-entry rendering must stay green as well; `/opt/git/v31_room_load/v31_native_rooms_entered.png` must not regress.
- Do not rely only on the focused navigator check before finishing.

Required offline regression gate:

```bash
JAVA_HOME=/opt/jdk-21.0.11+10 PATH=/opt/jdk-21.0.11+10/bin:$PATH \
LS_NATIVE_VISUAL_CASES=v1,v14 \
./gradlew :player-wasm:runWasmNativeVisualRegressionTest \
  -PoutputDir=/opt/git/LibreShockwave/build/native-visual-v1-v14-regression \
  --no-daemon
```

Required v31 room-entry non-regression gate:

```bash
JAVA_HOME=/opt/jdk-21.0.11+10 PATH=/opt/jdk-21.0.11+10/bin:$PATH \
LS_V31_ROOM_ENTER_SKIP_LEGACY_VISUALS=1 \
./gradlew :player-wasm:runWasmV31RoomEnterTest \
  -PoutputDir=/opt/git/LibreShockwave/build/roomEnterTestV31-post-navigator-fix \
  --no-daemon
```

## Acceptance Criteria
- The implementation uses the existing v31 native login regression flow from `wasm-native-visual-regression-test.js`.
- The v31 login flow uses the pinned SSO ticket `vibe-sso-admin-504d3ba4-acdb-4436-b67c-d0752f44f767`.
- The comparison screenshot is captured only after all observable networking has finished and the navigator has had a short additional settle period.
- The implementation must intentionally wait a bit after network idle before capturing the comparison screenshot.
- The comparison screenshot must not be taken on the first network-idle frame; it must be taken only after waiting a bit longer for the navigator to fully settle.
- The comparison screenshot must not be taken while `Problems Connecting` is visible or immediately on recovery from it; the harness must wait past that transient state and then capture only from the settled navigator frame.
- The actual navigator crop matches the crop from `/opt/git/v31_room_load/v31_native.png` exactly.
- The focused run saves navigator crop/diff artifacts for inspection.
- `v1` and `v14` offline native visual regressions remain green.
- The v31 room-entry visual state remains unchanged and still matches its own reference target.
- The fix is a real runtime/rendering fix, not a harness-side or post-render patch.
- Temporary diagnostics are removed before commit.
