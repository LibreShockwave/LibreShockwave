# Final Goal: Fix v1 Black Text Regression

## Objective

The only active goal is to fix the old v1 text-color regression: text in
representative `../v1_assets/` content that should render black must not be
turned white.

Do this without causing regressions anywhere else. In particular, preserve:

- MobilesDisco login/frontpage rendering, input, and click behavior
- old Director typing/click behavior for MobilesDisco and `../v1_assets/`
- Navigator, Messenger/Friends, Catalogue Collectables, and private-room
  broad-white overlay regressions documented in `/opt/git/MESSENGER_BUG.md`
- palette, bitmap, matte/background-transparent ink, text rendering, score,
  cast loading, and input behavior already covered by tests/artifacts

Do not solve this with movie-specific, URL-specific, member-name, script-name,
or coordinate hardcodes.

## Root Cause

The final root cause is text-color resolution for `BACKGROUND_TRANSPARENT`
text in:

```text
cpp/src/player/render/pipeline/SpriteBaker.cpp
```

There were two interacting problems:

1. `shouldUseSpriteBackColorForTransparentText()` was too broad. It converted
   any resolved black transparent text to non-black `backColor`, which turned
   v1 dynamic typed text white.
2. XMED text needed a narrower distinction. MobilesDisco XMED labels resolve
   to authored/default white and must stay white, while v1 non-bold
   Volter/Goldfish pixel-font XMED labels use the sprite's black `foreColor`
   as their intended foreground. Bold v1 pixel-font status/header text can
   remain white.

The no-alias v1 browser artifact shows the affected black-text shape in
`gf_private.dcr`:

```text
channel 90 loginname:    ink 36, foreColor 0, backColor 16777215, memberType text, intended black field text
channel 92 loginpwshow:  ink 36, foreColor 0, backColor 16777215, memberType text, intended black field text
channel 93 label text:   ink 36, foreColor 0, backColor 16777215, memberType xtra, intended black label text
channel 94 label text:   ink 36, foreColor 0, backColor 16777215, memberType xtra, intended black label text
```

The old backColor helper affected all three text bake paths:

```text
SpriteBaker::bakeDynamicText
SpriteBaker::bakeFileBackedText for XMED
SpriteBaker::bakeFileBackedText for STXT
```

The fix must resolve text color from authored/runtime text data and parsed
style, not from movie names, URLs, member names, or coordinates.

Important nuance: v1 itself has expected white transparent text too. The
`Loading Habbo Hotel...` text in v1 should remain white. Therefore the fix
must not become a broad "v1 text is black" rule. It must determine the intended
text color programmatically from authored/runtime text data, sprite foreground
state, parsed XMED/STXT style, member state, font/style metadata, or another
generic Director signal.

The loading/status case is part of the same final goal: v1
`Loading Habbo Hotel...` should be white because the authored or runtime text
state resolves to white, while other v1 labels/fields should remain black when
their authored or runtime text state resolves to black.

Current implementation direction:

- remove the broad transparent text `backColor` fallback
- preserve explicit/default white STXT/XMED text when no generic signal says
  sprite `foreColor` is intended
- allow non-bold parsed Volter/Director-pixel-font XMED text with black
  sprite `foreColor` to render black
- keep dynamic/runtime explicit white text white, including v1 loading/status
  text such as `Loading Habbo Hotel...`

## Required Fix Status

1. Fixed `SpriteBaker` text-color selection so old v1 black runtime text and
   non-bold pixel-font XMED text remain black.
2. Preserved MobilesDisco's white transparent login/frontpage text where that is
   genuinely authored/expected.
3. Kept the solution generic. Do not key off:
   - `gf_private.dcr`
   - MobilesDisco
   - URLs or file paths
   - member names such as `loginname`, `loginpwshow`, `info_btn`, or labels
   - screen coordinates
   - script names
4. Updated/added focused tests that prove both sides:
   - v1-style `BACKGROUND_TRANSPARENT` text with black resolved text and white
     `backColor` remains black
   - v1 loading/status text such as `Loading Habbo Hotel...` that should be
     white remains white through its resolved authored/runtime text color, not
     through a blanket old-version or path-specific exception
   - MobilesDisco-style transparent text that should be white remains white
5. Browser output has been verified for v1 and MobilesDisco with the rebuilt
   WASM bundle. Full native suite still stops at the known unrelated
   `testBuiltinRegistryFoundation` assertion before the renderer tests.

## Final Evidence

No-alias v1 browser evidence uses the real directly served movie:

```text
http://localhost/dcr0910/gf_private.dcr
```

Latest captured artifacts:

```text
artifacts/v1_assets/direct_v1_scan_textstyle_debug.json
artifacts/v1_assets/gf_private_input_click_no_alias_verification.json
artifacts/v1_assets/gf_private_input_click_no_alias_sprites.json
artifacts/v1_assets/gf_private_input_click_no_alias_canvas.png
artifacts/mobilesdisco/login_visual_after_textstyle_debug_cdp.json
artifacts/mobilesdisco/current_debug_sprites_after_textstyle_debug.json
artifacts/mobilesdisco/current_canvas_after_textstyle_debug.png
artifacts/text_color_regression/ui_regression_quick/navigator.png
artifacts/text_color_regression/ui_regression_quick/private_room.png
artifacts/text_color_regression/ui_regression_quick/summary.json
```

The flow clicked the `loginname` field, typed `abc`, Backspace-edited to `ab`,
Tabbed to `loginpwshow`, typed `xy`, and clicked the visible
`ok_dialog_btn_e active` bitmap button. After the click, the OK button left
the active sprite set and selection cleared.

Final color evidence from the refreshed browser artifacts:

```text
v1 gf_private:
- loginname typed text `ab`: rgb 0
- loginpwshow typed text `xy`: rgb 0
- non-bold Volter labels such as `Name of your Habbo`, `Password`, and
  `Forgotten your password?`: rgb 0
- bold Volter status/header text such as `Connecting...`: rgb 16777215

MobilesDisco:
- `ENTER THE DISCO:`, `User :`, `Password :`, `Login!! >>`, and
  `CREDITS <<`: rgb 16777215
```

Focused native evidence covers the transient v1 loading/status requirement
that is difficult to catch reliably in a browser screenshot:

```text
./cmake-build-debug/cpp/libreshockwave_tests TransparentTextColorFoundation
```

That test proves transparent runtime text with resolved black text color stays
black, and transparent runtime text with resolved white text color, including
`Loading Habbo Hotel...`, stays white. This is the programmatic guard that
prevents a blanket old-version or path-specific black-text rule.

Latest `localhost:3000` harness smoke evidence:

```text
artifacts/text_color_regression/ui_regression_quick/navigator.png
artifacts/text_color_regression/ui_regression_quick/private_room.png
artifacts/text_color_regression/ui_regression_quick/summary.json
```

Those smoke captures confirm the harness still loads and the private-room
furniture/compositor path remains visually coherent. The Messenger/Catalogue
clicks in this quick pass did not open those panels, so they are not claimed as
final Messenger/Catalogue proof here.

Port note: `localhost:3000` was already occupied during verification, so the
same rebuilt `cmake-build-wasm/cpp/wasm-dist` bundle was also served on
`127.0.0.1:3001` for clean-cache browser captures.

## Regression Coverage To Preserve

Before changing renderer, bitmap, text, palette, score, input, VM, or
cast-loading behavior, read:

```text
/opt/git/MESSENGER_BUG.md
```

That file documents regression coverage for:

- Navigator broad white backing
- Messenger/Friends panel layout and list backing
- Catalogue Collectables broad white product-list backing
- private-room broad white playfield/compositor overlay
- private-room couch/furniture state tracking

Do not reintroduce broad white overlays, missing matte transparency, broken
script-created UI backings, broken catalogue art, or private-room compositor
white-key regressions.

MobilesDisco must also remain valid regression coverage:

```text
http://localhost/mobilesdisco/dcr_0519b_e/20000201_mobiles_disco.dcr
http://localhost:3000/
```

Important MobilesDisco artifacts from the previous investigation:

```text
artifacts/mobilesdisco/current_canvas_after_d6_score_yoffset.png
artifacts/mobilesdisco/current_debug_sprites_after_d6_score_yoffset.json
artifacts/mobilesdisco/login_visual_after_d6_score_yoffset_cdp.json
artifacts/mobilesdisco/input_after_authored_text_field_semantics_cdp.json
artifacts/mobilesdisco/current_debug_sprites_after_authored_text_field_semantics.json
artifacts/mobilesdisco/current_canvas_after_authored_text_field_semantics.png
artifacts/mobilesdisco/current_debug_sprites_after_textstyle_debug.json
artifacts/mobilesdisco/current_canvas_after_textstyle_debug.png
```

MobilesDisco notes that must remain true:

- the login/frontpage composition still reaches the expected old Director
  screen
- character figures stay in the correct skin/hair/yellow color family, not the
  older grayscale regression
- `CREDITS <<` is the correct authored/reference text, not `CREDITS >>`
- login fields still focus, type, Backspace, and Tab through the generic old
  Director text path
- login/registration/modify/credits click targets still route through normal
  sprite/event dispatch

Old Director input hardcode constraints still apply. Do not reintroduce
editability inference from:

```cpp
sprite->legacyProperty("behaviorparam:maxlength")
sprite->legacyProperty("behaviorparam:loginpw")
sprite->legacyProperty("behaviorparam:loginpwshow")
sprite->legacyProperty("behaviorparam:isloginfield")
(!member.name().empty() && memberText(member).empty())
inferrededitablefield
```

## Verification Requirements

Run focused native and browser verification after the fix.

Required build/test commands:

```bash
cmake --build cmake-build-debug --target libreshockwave_tests libreshockwave_render_probe -j2
cmake --build cmake-build-wasm --target libreshockwave_cpp_wasm_dist -j2
./cmake-build-debug/cpp/libreshockwave_tests
```

The full native test binary has recently aborted later at the known unrelated
assertion:

```text
cpp/tests/sdk_foundation_test.cpp:6376:
void testBuiltinRegistryFoundation():
Assertion `perTargetContentCounts == std::vector<int>({1, 1})' failed.
```

If that still occurs, record it as a known later blocker only after confirming
the relevant text-color and old Director input/render tests ran before it. In
the latest run this assertion occurred before the renderer tests, so final
text-color proof comes from the focused `TransparentTextColorFoundation` target
plus the browser artifacts above.

Required browser checks:

1. `http://localhost/dcr0910/gf_private.dcr` through `http://localhost:3000/`
   - v1 labels and typed field text that should be black render black
   - typing/click flow still works
2. MobilesDisco through `http://localhost:3000/`
   - white login/frontpage text that should remain white still remains white
   - visual layout does not regress
   - old Director typing and click behavior still works
3. Regression flows from `/opt/git/MESSENGER_BUG.md` where renderer/text/ink
   changes could affect them

Use native-scale screenshots and debug sprite artifacts where visual evidence
matters. Save new evidence under `artifacts/`.

## Acceptance

This goal is complete only when:

- v1 black text no longer renders white
- MobilesDisco text does not regress
- old Director typing/click behavior still works in browser coverage
- existing renderer and UI regression flows are not broken
- tests or documented browser artifacts prove the above

Anything else is supporting context, not a separate active goal.
