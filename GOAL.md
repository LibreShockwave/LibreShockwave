# LibreShockwave debugger replay

## Objective

Make `/opt/git/libreshockwave-bugs/cantenterarena.lswdebug` replay to its
recorded arena-like final state, support
`libreshockwave_debugger --play <movie-or-recording>`, preserve unrelated
worktree changes, and verify the full 30.568-second replay with a rendered
frame after at least 40 seconds.

## Current progress

- `--play` parsing and recording startup are present in `debugger/cpp/main.cpp`
  and `DebuggerWindow.*`.
- The debugger-side error interception/deferred handler replay workaround was
  removed.
- The VM/network path now tracks pending external cast fetches, holds opaque
  encrypted multiuser bytes while those casts are being applied, and resumes
  delivery only after the cast slots are loaded. Multiuser queue callbacks
  also recheck the dependency boundary between messages.
- `libreshockwave_debugger --play
  /opt/git/libreshockwave-bugs/cantenterarena.lswdebug` was run for 65 seconds
  (the recording is 30.568 seconds). The rendered final frame at
  `/tmp/libreshockwave-replay-final.4bBLZv/final.png` showed the recorded
  Snowwar arena scene. The full CMake build and `git diff --check` passed.
