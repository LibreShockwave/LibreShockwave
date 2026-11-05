# Performance tracking: 05/11/26

Working log for the optimisation pass driven by `docs/OPTIMISATION.md`.
Each entry records what changed, the files touched, the measured impact,
and the test results. Replay evidence comes from
`/opt/git/libreshockwavebugs/bad-client-fps.lswdebug` (291 events,
86.9s movie time) run in the Release desktop debugger.

## Baseline profile (pre-optimisation)

Steady-state replay clock: about 789 KIPS (thousand instructions per
second). Per-opcode timings from the harness profile:

- getGlobal 0.108 us
- pushInt8 0.017 us
- ltEq 0.028 us
- joinStr 0.129 us
- getMovieProp 0.342 us
- get 0.405 us

Fused arglist calls account for 230.9s of the roughly 230s steady
clock. That is app-authored game logic. It is off-limits for C++
optimisation. The "profile total" in the harness output is a
double-counted nested sum. It is never cited as wall-clock time.

## 1. can_suspend_ fast-path flag

- Files: `trace_listener.hpp`, `debug_controller_api.hpp`,
  `lingo_vm.cpp`.
- Change: `TraceListener` carries a `can_suspend_` flag. The
  per-instruction cooperative-pause check now reads the flag before
  making the virtual `isExecutionSuspended()` call. Listeners that can
  never suspend (the default) skip the virtual call entirely.
  `DebugControllerApi` sets the flag in its constructor.
- Impact: removes one virtual call per instruction on every build
  where tracing is disabled or the listener cannot suspend.
- Tests: `ctest --test-dir cmake-build-debug --output-on-failure`
  100% (3/3, 0.89s).

## 2. Inline fast path in executeInstruction

- Files: `lingo_vm.cpp`, `opcode_registry.hpp`, `opcode_registry.cpp`.
- Change: `executeInstruction` inlines a switch over the handful of
  opcodes it handles directly. The path is guarded by
  `OpcodeRegistry::isDefaultRawHandler()`, which is true only while an
  opcode's raw handler is still the un-overridden built-in default.
  `registerHandler()` overrides therefore still take effect. The guard
  also lets the inline path skip the variable-scaling setup for
  opcodes that do not use it.
- `isDefaultRawHandler` compares the live handler against a
  `default_raw_handlers_` snapshot taken at the end of the registry
  constructor.
- Impact: the four local/param opcodes and the remaining inline
  opcodes no longer fall through the generic handler lookup on the
  hot path.
- Tests: `ctest --test-dir cmake-build-debug --output-on-failure`
  100% (3/3, 0.89s). The custom-handler override regression test
  (PUSH_ZERO override at `sdk_foundation_test.cpp:15105`) passes.

## 3. pushArgList hoist

- Files: `opcode_registry.cpp`.
- Change: `pushArgList` and `pushArgListNoRet` hoist
  `context.argument()` into a local `arg_count` before the
  immediate-call attempts. The immediate-call paths may pop the
  argument slot. Hoisting keeps the count valid for the fallback
  `popArgs` path.
- Impact: removes one redundant `argument()` read and keeps the
  fallback correct without re-reading a slot that may have moved.
- Tests: `ctest --test-dir cmake-build-debug --output-on-failure`
  100% (3/3, 0.89s).

## Replay wall-clock

- Run 1 (600s budget): timed out without completing. About 90% CPU
  throughout. Game socket established. No MUS socket. Frames
  presenting.
- Run 2 (30-min budget, frame dump): in progress at the time of this
  entry. The pre-optimisation baseline wall-clock was never captured.
  Run 1 only proved the replay takes longer than 600s.

## Remaining items from OPTIMISATION.md

- Avoid temporary `std::vector<Datum>` construction where span-backed
  calls are semantically safe.
- Avoid copying `Datum` results when moving local temporaries is safe.
- Avoid copying script-instance receiver datums for read-only property
  paths.
- Avoid string materialization in equality, lookup, and collection
  comparisons. (Partially done: commit d70ce65f covered prop-list key
  lookups.)
