# Optimisation: reduce slow Lingo VM allocation-heavy arithmetic dispatch

## Problem

The browser harness has reported slow inclusive Lingo VM work in the
login/security callback path, especially authored big-number arithmetic such as
`powMod` and its callers. The VM work has improved substantially, but the last
valid profile evidence still showed the security path above the target range.

The latest harness warning output still shows the security path dominated by
authored modular arithmetic. The next work should specifically make these
reported slow handlers faster through generic VM/runtime improvements, not by
hardcoding the handler names:

- tick: 2723.0ms
- `powMod`: 2455ms / 2448ms, 4387 instructions
- `responseWithPublicKey`: 2477ms, 1557 instructions
- `securityCastDownloadCallback`: 2477ms, 8 instructions
- `DoCallBack`: 2478ms, 51 instructions
- `removeCastLoadTask`: 2479ms, 33 instructions
- `DoneCurrentDownLoad`: 2483ms, 61 instructions
- `update`: 2483ms, 85 instructions
- `prepare`: 2484ms, 57 instructions
- `prepareFrame`: 2487ms, 208 instructions
- socket message: 2814.0ms
- `handleServerSecretKey`: 2812ms, 166 instructions
- `forwardMsg`: 2812ms, 84 instructions
- `msghandler`: 2814ms, 129 instructions
- `xtraMsgHandler`: 2814ms, 64 instructions
- host xtra callbacks: 2814ms
- multiuser message: 2814ms

The important symptom is generic VM overhead: many authored instructions and
small calls spend too much time in dispatch, argument setup, property/list
access, `Datum` copying, string conversion, and allocator churn. Fixes should
continue to reduce those generic runtime costs.

## Constraints

- Do not add harness-specific, room-specific, page-specific, handler-name, or
  asset-name shims.
- Do not hardcode authored Lingo handlers in C++.
- Do not add native C++ branches for `BigInt`, `HugeInt`, `HugeInt15`,
  `powMod`, `Modulo`, `div`, `getString`, or `getByteArray`.
- Keep fixes generic: VM dispatch, handler dispatch, stack/local access,
  builtins, list/property-list behavior, script-instance properties, string
  and chunk helpers, cache behavior, and allocation/copy reduction.
- Preserve authored script-instance handlers.
- Do not commit credentials, SSO tickets, endpoint changes, generated harness
  HTML, or test-only host substitutions.
- Commit after every change once it is verified so each optimisation step remains
  reviewable and bisectable.
- Commit messages must not mention game, harness, room, user, asset, or
  endpoint names.

## Current Direction

Continue looking for generic hot-path cleanup with a clear connection to the
remaining slow VM path:

- avoid temporary `std::vector<Datum>` construction where span-backed calls are
  semantically safe;
- avoid copying `Datum` results when moving local temporaries is safe;
- avoid copying script-instance receiver datums for read-only property paths;
- avoid string materialization in equality, lookup, and collection comparisons;
- preserve all existing Director/Lingo semantics and authored handler dispatch.

## Verification

For scoped VM/runtime changes, run:

```bash
cmake --build cmake-build-debug --target libreshockwave_tests -j2
cmake --build cmake-build-wasm --target libreshockwave_cpp_wasm_dist -j2
cmake --build cmake-build-wasm-profile --target libreshockwave_cpp_wasm_dist -j2
```

The full native test binary is still expected to stop at the known
`sdk_foundation_test.cpp:24692` duration assertion unless that unrelated issue
is fixed separately.

Use harness/profile captures only as timing evidence when they collect real
debug events and do not redirect to a concurrent-login/disconnected page.
Use `http://localhost:3000/venus-quackster-harness` for browser harness checks,
and inspect warning slow-method output when deciding the next generic VM/runtime
hot path to optimise.
