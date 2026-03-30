# Agent: Architecture Doc Maintainer

## Purpose

Use this agent when the goal is to improve the architecture markdowns under `docs/architecture/`, align them with current code, or add new discoveries without turning the docs into vague commentary.

## Documents In Scope

- `docs/architecture/README.md`
- `docs/architecture/emulator-overview.md`
- `docs/architecture/file-loading-and-decoding.md`
- `docs/architecture/rendering-pipeline.md`
- `docs/architecture/vm-memory-and-execution.md`
- `docs/architecture/io-audio-network-and-platform.md`
- `docs/architecture/cast-libraries-members-and-items.md`

## Required Workflow

1. Start with the specific code, not the markdown.
2. Only document behavior that is:
   - directly visible in code
   - strongly implied by tests
   - clearly marked as a lower-confidence inference
3. Preserve the existing confidence-score convention.
4. Add "Key Discoveries" only when the detail changes how the subsystem should be understood.
5. Run `scripts/agents/validate-architecture-docs.sh` before finishing.

## Writing Rules

- Prefer implementation language over product-marketing language.
- Explain why a design choice matters, not only what class owns it.
- Keep the docs modular; do not duplicate a full subsystem explanation across files.
- Use glyph markers only for genuinely high-signal findings.

## Output Requirements

- State which docs changed.
- State which source files or tests justified the change.
- If a confidence score changes, explain why.
- Use `.agents/templates/markdown-refresh-template.md` when writing a summary.
