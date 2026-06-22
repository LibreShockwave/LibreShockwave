# Goal: Fix Habbo avatar shadow color

## Reproduction

1. Open `http://localhost:3000/venus-quackster-harness`.
2. Wait for the Hotel Navigator to appear.
3. Click the `Go` text to the right of `Welcome Lounge`.
4. In the Welcome Lounge, click the center room tile.
5. Wait 10 seconds.
6. Take a screenshot.

## Expected

The Habbo avatar shadow should be a neutral dark gray/black oval, matching
`artifacts/expected_colour_shadow.png`.

## Reported

The shadow under the Habbo avatar appeared blue-tinted in a page screenshot.

## Verification Notes

- `artifacts/actual_blue_shadow.png` shows the reported blue pixels under the
  Habbo in a page screenshot.
- Page screenshots include harness padding, so raw canvas captures are required
  for pixel-accurate comparison with `artifacts/expected_colour_shadow.png`.
- The red/brown `blockhilite` hover/title outline can be ignored for this
  issue.
- Live sprite diagnostics showed the separate authored shadow sprite
  `sh_std_sd_1_0_0` was baked blue before final stage blending.

## Status

Complete for the reported blue-shadow defect. The output must not have the Habbo
shadow rendered as blue, and the current verified output does not.

The rejected heuristic `copyPixels` approach was removed. The root cause was in
indexed matte remapping for authored black/white shadow masks. The live
`sh_std_sd_1_0_0` sprite uses `ink=8`, `blend=16`, `foreColor=255`, and
`backColor=0`. Before the fix, the indexed matte remap interpreted
`foreColor=255` as literal packed RGB `0x0000ff`, turning the authored black
shadow bitmap blue before stage blending.

The fix preserves already-masked black/transparent indexed matte art when the
legacy default remap would otherwise turn it blue. This keeps authored `std` /
`sd` shadow PNGs dark while leaving richer indexed artwork, such as room floor
tiles, on the existing remap path.

## Latest Verification

- `artifacts/current_shadow_debug_canvas.png` and
  `artifacts/current_shadow_debug_sprites.json` capture the pre-fix root cause:
  `sh_std_sd_1_0_0` baked foreground pixels were `rgb: 255` (`0x0000ff`).
- `artifacts/shadow_fixed_canvas.png` captures the verified fixed room output.
- `artifacts/shadow_fixed_sprites.json` shows `sh_std_sd_1_0_0` baked foreground
  pixels as `rgb: 0` (`0x000000`), with the same `ink=8` and `blend=16`.
- `artifacts/shadow_fixed_shadow_rect.png` is the final shadow rect. Pixel
  analysis found `0` blue-ish pixels; the dominant blended shadow/floor color is
  `#866947`.
- `artifacts/shadow_fixed_normal_canvas.png` captures the same flow on the
  normal harness URL, and `artifacts/shadow_fixed_normal_shadow_rect.png` also
  has `0` blue-ish pixels with dominant blended shadow/floor color `#866947`.

`cpp/tests/sdk_foundation_test.cpp` includes a focused regression for the indexed
matte black-mask remap case. The native test target builds, but the full test
executable still stops earlier at the pre-existing unrelated
`testBuiltinRegistryFoundation` assertion:
`perTargetContentCounts == std::vector<int>({1, 1})`.

## Completion Audit

Rechecked the current worktree and artifacts on 2026-06-22:

- `cmake --build cmake-build-debug --target libreshockwave_tests -j2` passes.
- `cmake --build cmake-build-wasm --target libreshockwave_cpp_wasm_dist -j2`
  passes.
- `artifacts/shadow_fixed_sprites.json` contains one `sh_std_sd_1_0_0` sprite
  with `ink=8`, `blend=16`, `foreColor=255`, `backColor=0`, and baked
  `topColors[0].rgb == 0`.
- `artifacts/shadow_fixed_normal_canvas.png` has `0` blue-ish pixels in the
  verified shadow rect; the dominant blended floor-shadow color remains
  `#866947`.
- Running `./cmake-build-debug/cpp/libreshockwave_tests` still aborts at the
  unrelated pre-existing `testBuiltinRegistryFoundation` assertion before the
  new indexed matte regression can execute.
