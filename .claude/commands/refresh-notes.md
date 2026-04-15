---
name: "Refresh Notes"
description: Restart the local notes server after regenerating site inputs
category: Documentation
tags: [docs, notes, mkdocs, refresh]
---

Restart the local notes site after editing documentation. This command regenerates site inputs, stops the old server on the notes port, and starts a fresh background `mkdocs serve`.

**Input**: No arguments required. Run `/refresh-notes` after any doc change that should appear in the notes site.

This command is now a compatibility wrapper around `scripts/serve-notes.sh`.

## When To Use

- After editing `notes/**/*.md`
- After editing `docs/requirements/*.md`
- After editing `mkdocs.yml`
- After adding a new `notes/` page, a new `notes/` subdirectory, or a new `notes/tools/*.md`
- After any docs task where you want the browser page to reflect the latest nav/content immediately

## Steps

1. Run:

```bash
scripts/refresh-notes.sh
```

2. Confirm that:

- `mkdocs.gen.yml` was regenerated
- any old listener on the notes port was stopped
- a fresh `mkdocs serve` process was started in the background
- the command reports URL, PID, and log path

3. Report a concise summary:

- refreshed successfully
- any generated files changed
- any missing prerequisite such as `python3`

## Expected Result

The browser should see the updated documentation after the restarted service comes back up.

## Guardrails

- If restart fails, report the exact failing step
