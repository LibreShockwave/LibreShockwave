# Agent Index

This directory defines task-specific agent playbooks for the LibreShockwave repository.

## Available Agents

- `code-efficiency-auditor.md`
  - Finds runtime inefficiencies, cache misses, validation gaps, and unnecessary work in hot code paths.
- `architecture-doc-maintainer.md`
  - Keeps the architecture markdown set accurate, readable, and tied to current code discoveries.

## Supporting Files

- `checklists/code-efficiency-checklist.md`
- `checklists/markdown-update-checklist.md`
- `templates/efficiency-report-template.md`
- `templates/markdown-refresh-template.md`

## Helper Scripts

- `../scripts/agents/run-efficiency-scan.sh`
- `../scripts/agents/validate-architecture-docs.sh`

## Usage Pattern

1. Pick the agent that matches the task.
2. Read its checklist before editing files.
3. Use the helper scripts for repeatable validation.
4. Write findings or summaries in the structure from the matching template.
