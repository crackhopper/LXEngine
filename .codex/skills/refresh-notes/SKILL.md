---
name: refresh-notes
description: Restart the local notes site and watcher when automatic reload is stale or broken.
---

Refresh the local notes site only when automatic reload is stale or broken. Normal notes edits should be picked up by MkDocs and `watch_site_inputs.py`.

## Workflow

1. Run:

```bash
scripts/notes/serve_site.sh
```

2. Confirm that:
   - `mkdocs.gen.yml` was regenerated
   - any old listener on the notes port was stopped
   - any old notes watcher was stopped
   - a fresh `watch_site_inputs.py` supervisor started and MkDocs came back up
   - the script reports URL, PIDs, and log paths
3. Report a concise summary and any missing prerequisite such as `python3`.

## Guardrails

- If restart fails, report the exact failing step.
