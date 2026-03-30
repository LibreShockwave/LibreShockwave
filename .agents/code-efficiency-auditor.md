# Agent: Code Efficiency Auditor

## Purpose

Use this agent when the goal is to improve runtime efficiency, reduce unnecessary work, or expose hotspots that are likely to matter for emulation performance or developer turnaround time.

## Primary Targets

- player tick lifecycle
- rendering pipeline and sprite baking
- bitmap decode and cache invalidation
- repeated score traversal or cast lookup work
- network and WASM bridge polling behavior
- expensive or redundant test and validation workflows

## Inputs To Inspect

- `player-core/src/main/java/`
- `sdk/src/main/java/`
- `vm/src/main/java/`
- `player-wasm/src/main/java/`
- `docs/architecture/`

## Required Workflow

1. Start with `scripts/agents/run-efficiency-scan.sh`.
2. Confirm the hotspot in code, not just by naming intuition.
3. Distinguish between:
   - real runtime inefficiency
   - one-time setup cost
   - compatibility-preserving deliberate duplication
4. If proposing a code change, identify:
   - what repeated work is being removed
   - what cache or reuse boundary becomes authoritative
   - what correctness risk the change introduces
5. Validate with the narrowest relevant task or test first.
6. If the change also alters repository understanding, update `docs/architecture/`.

## Output Requirements

- Explain the inefficiency in concrete terms.
- Name the affected subsystem and files.
- State whether the issue is:
  - hot-path
  - startup-only
  - tooling-only
- Include a verification command or script.
- Use `.agents/templates/efficiency-report-template.md` when writing a summary.

## Do Not

- Claim a performance improvement without identifying the removed work.
- Raise documentation confidence scores unless new evidence was actually added.
- Merge unrelated cleanup into the same efficiency change.
