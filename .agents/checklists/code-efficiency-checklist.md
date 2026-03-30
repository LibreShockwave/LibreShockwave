# Code Efficiency Checklist

- Run `scripts/agents/run-efficiency-scan.sh`.
- Identify the subsystem before proposing a fix.
- Confirm whether the work is per tick, per frame render, per load, or per validation run.
- Check whether a cache, revision counter, or lazy parse path already exists.
- Verify that the proposed change preserves Director compatibility assumptions.
- Run the narrowest relevant validation command.
- Update docs only if the change improves architectural understanding.
