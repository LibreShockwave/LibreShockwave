# VM, Memory, And Execution Model

## 1. Lingo VM Architecture

LibreShockwave executes Lingo through a bytecode interpreter implemented in `vm`. The current VM is not a JIT and it is not a source-level interpreter. It works by:

- resolving a handler
- creating an execution context
- stepping bytecode instructions
- mutating runtime state through builtins, property providers, and script instances

`LingoVM` is the top-level coordinator. It owns:

- global variables
- preference values
- the call stack
- builtin registries
- opcode registries
- handler caches
- deferred task queues
- error and stop-event state
- tracing and alert-hook integration

The result is a VM that behaves like a bounded runtime service embedded inside the player rather than a free-floating language engine.

## 2. Handler Resolution

Handler lookup follows compatibility-oriented precedence rules.

Important points:

- global script handlers can take precedence over builtins
- handler lookup searches the main movie and loaded external casts
- missing lookups are cached to avoid repeated negative searches
- receiver-aware calls can treat the first script instance argument as implicit receiver state

This matters because many Director movies use a mixed model of movie scripts, behavior scripts, parent scripts, xtras, and builtin language features. The VM has to make those coexist predictably.

## 3. Execution Context And Scope

Each handler call executes inside a scope that provides operand stack and local variable state for that invocation. `ExecutionContext` is reused for the duration of a handler run rather than rebuilt per instruction.

At a conceptual level, the runtime stack looks like this:

- global VM state
- call stack of scopes
- current handler execution context
- script-instance receiver state when applicable

This is why a handler can simultaneously access:

- movie-global values
- arguments and locals
- receiver properties
- builtin services

## 4. Opcode And Builtin Surface

The opcode registry is split into functional groups rather than one monolithic dispatch table. Current categories include:

- stack operations
- arithmetic
- comparisons
- logical operations
- strings
- variables
- control flow
- lists
- calls
- properties

Builtin coverage is similarly structured and currently includes groups such as:

- math
- strings
- lists
- constructors
- output
- networking
- xtras
- casts
- sprites
- type helpers
- control flow
- timeout
- image
- sound

That organization makes the VM easier to evolve because compatibility fixes can usually be contained in the correct builtin or opcode family rather than spread across unrelated runtime code.

## 5. Safety Limits And Failure Boundaries

The VM includes several guardrails so that movie code cannot run forever without detection.

Current protections include:

- maximum call stack depth
- per-handler hard timeout
- tick deadline enforcement from the player
- periodic safepoints during long instruction runs
- reentrancy protection for sensitive flows such as destruction

When an error occurs, the VM captures Lingo stack context, exposes trace notifications if enabled, and can route the failure through an alert hook. This is important for debugging because runtime failures are not treated as generic Java exceptions with all Lingo context lost.

## 6. Runtime Service Injection

The VM does not own networking, sprites, casts, movie properties, or sound directly. Those services are installed by the player as thread-local providers around each tick.

That provider set currently includes:

- networking
- xtras
- movie properties
- sprite properties
- cast libraries
- timeouts
- update callbacks
- external parameters
- sound
- the active palette used by datum color resolution

This is one of the cleaner architectural choices in the project. It lets the VM and builtin layer stay reusable while still binding each handler execution to the current player state.

## 7. Deferred Work

Not every VM-side action is performed immediately.

The runtime keeps queues for:

- deferred script-instance calls
- deferred tasks that should only run after the current stack unwinds

This design reduces reentrancy hazards and keeps some Director-like behavior aligned with frame boundaries and script dispatch rules.

Operationally, it means the user-visible result of a handler can depend on end-of-tick flushing rather than purely on the current opcode position.

## 8. Memory Ownership Domains

There is no single "emulator memory" bucket. Memory is distributed across several ownership domains:

- `DirectorFile`
  - raw container bytes, chunk metadata, lazily reparsed chunk payloads, decoded resources
- cast libraries and cast members
  - file-backed definitions, dynamic member instances, palette overrides, rendered text caches
- sprite registry
  - mutable runtime state for active or dynamic channels
- bitmap caches
  - processed bitmap outputs and palette-sensitive derived images
- VM state
  - globals, stack frames, handlers, and temporary datums
- platform bridges
  - pending network jobs, queued WASM messages, audio buffers

That split is healthy. It prevents the player from having to choose between two bad options: either caching nothing and becoming slow, or flattening everything into one state bag and becoming impossible to invalidate correctly.

## 9. Director File Memory Strategy

`DirectorFile` is not only a parser. It is also a memory boundary.

Notable behaviors include:

- support for raw and Afterburner-compressed inputs
- lazy chunk reparsing from stored raw bytes
- direct lookup helpers for score, text, bitmap, palette, and script resources
- release of non-essential chunk data for memory reduction

That last point is especially important. The parser can deliberately free heavyweight data such as non-essential raw chunk payloads after they are no longer needed. The emulator therefore does attempt memory discipline rather than keeping the full original file materialized forever.

## 10. Cache Invalidation Model

The system uses several independent invalidation mechanisms.

Examples:

- bitmap cache entries are sensitive to palette version and render-relevant settings
- dynamic bitmap members are treated as mutable and are not assumed cache-safe
- sprite registry revision changes invalidate render assumptions when sprite state changes
- handler caches and missing-handler caches avoid repeated lookups but are cleared when new script visibility appears
- retired dynamic member slots trigger cleanup paths so stale sprite bindings do not survive indefinitely

This is one of the core engineering strengths of the project. The code does not rely on "hope-based caching". It explicitly tracks when cached outputs stop being valid.

## 11. What Memory Means In Practice

When people ask about "memory" in this emulator, the answer is not just heap usage. The real question is how long state is considered authoritative.

In the current implementation:

- score data is authoritative for authored defaults
- sprite state is authoritative for live runtime mutations
- cast members are authoritative for member-level mutable resources
- the VM is authoritative for script-visible values and execution state
- caches are authoritative only until their revision rules are violated

That distinction explains many bugs. A rendering issue may look visual but actually be a stale cache problem. A sprite reset issue may look like a timeline bug but actually be a runtime ownership bug.

## Confidence Score

- VM execution model: `9.1/10`
- Runtime service injection model: `8.9/10`
- Memory ownership model: `8.8/10`
- Cache invalidation behavior: `8.7/10`

Reason for score: the core VM path is strongly evidenced by `LingoVM`, `ExecutionContext`, the builtin and opcode registries, player tick integration, and the provider setup path in `Player`. Confidence is lower around total memory characteristics because JVM, WASM, and movie-specific workloads can still change practical behavior even when the ownership model is clear.
