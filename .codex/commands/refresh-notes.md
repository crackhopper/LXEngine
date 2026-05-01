---
name: "Refresh Notes"
description: Restart the local notes server and watcher when automatic reload is not enough
category: Documentation
tags: [docs, notes, mkdocs, refresh]
---

Restart the local notes site and watcher. Normal documentation edits should hot reload automatically; use this only when the watcher failed, dependencies changed, or the local preview process is stale.

**Input**: No arguments required.

This command runs `scripts/notes/serve_site.sh`, which regenerates `mkdocs.gen.yml`, stops the old listener and old supervisor, then starts a fresh `watch_site_inputs.py` supervisor that manages MkDocs.

## When To Use

- The browser stops updating after edits.
- `.tmp/notes-watch.log` shows repeated errors after you have fixed the input files.
- Python, MkDocs, theme, or script dependencies changed.
- You need to move the preview server to a different port.

## Steps

1. Run:

```bash
scripts/notes/serve_site.sh
```

The script is idempotent: if an old `mkdocs serve` is already bound to the
notes port, it is stopped first before the new one starts. No separate
`refresh-notes.sh` helper exists; do not invent one.

2. Confirm that:

- `mkdocs.gen.yml` was regenerated
- any old listener on the notes port was stopped
- any old notes watcher was stopped
- a fresh `watch_site_inputs.py` supervisor was started and MkDocs came back up
- the command reports URL, PID, and log paths

3. Report a concise summary:

- refreshed successfully
- MkDocs and watcher PID/log paths
- any missing prerequisite such as `python3`

## Expected Result

The browser should see the updated documentation after the restarted service comes back up.

## Guardrails

- If restart fails, report the exact failing step
