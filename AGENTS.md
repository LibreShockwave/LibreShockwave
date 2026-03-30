# Repo Agents

This repository does not use implicit agent behavior. Any agent work should follow the files in `.agents/` so that code review, efficiency work, and markdown maintenance stay consistent.

## Layout

- `.agents/README.md`
  - Index of the available agents and how to use them.
- `.agents/code-efficiency-auditor.md`
  - Runtime and code-efficiency analysis workflow.
- `.agents/architecture-doc-maintainer.md`
  - Documentation refresh workflow for the architecture markdown set.
- `.agents/checklists/`
  - Short operational checklists for repeatable runs.
- `.agents/templates/`
  - Output templates for reports and doc refresh summaries.
- `scripts/agents/`
  - Helper scripts that give agents deterministic validation entry points.

## Default Rules

1. Read the relevant file in `.agents/` before changing code or docs.
2. Prefer evidence from the local repository over inference.
3. Do not mix code-efficiency changes and markdown-refresh changes in one commit unless the user explicitly asks for a coupled change.
4. When updating `docs/architecture`, keep the confidence sections honest. Raise a score only when the new text is backed by code or tests.
5. Use the scripts in `scripts/agents/` for repeatable validation instead of retyping ad hoc commands when possible.

## Recommended Agent Selection

- Use `code-efficiency-auditor.md` when the task is to find waste, hot paths, cache gaps, repeated work, or validation bottlenecks.
- Use `architecture-doc-maintainer.md` when the task is to improve `docs/architecture` or align docs with the current runtime.
- Use both only when the user explicitly wants docs updated as part of an efficiency pass.
