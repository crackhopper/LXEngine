---
name: "Sync Design Docs"
description: Sync AGENTS.md and CLAUDE.md design indexes with notes/subsystems/
category: Documentation
tags: [docs, design, sync]
---

Synchronize the design-document indexes in `AGENTS.md` and `CLAUDE.md` with `notes/subsystems/`. Treat `notes/subsystems/` as the current system-design entrypoint.

**Input**: No arguments required. Run `/sync-design-docs` to perform a full sync.

**Steps**

1. **Scan both sides**

   - Read `AGENTS.md` and find the `## Design Documents` section (create it if missing, place it before `## Conventions`).
   - Read `CLAUDE.md` and find the `## Design Docs Index` section (create it if missing, place it after `## Specs Index`).
   - List all `*.md` files under `notes/subsystems/`, excluding `index.md` only if you explicitly decide it should not be indexed.

2. **Diff the two sides**

   Compare the set of docs referenced in `AGENTS.md` and `CLAUDE.md` against the files in `notes/subsystems/`.

   Classify each entry into one of:
   - **In sync**: exists in `notes/subsystems/` and is referenced in both index sections
   - **Missing from indexes**: file exists in `notes/subsystems/` but missing from `AGENTS.md` and/or `CLAUDE.md`
   - **Stale index entry**: referenced in `AGENTS.md` or `CLAUDE.md` but no corresponding file exists in `notes/subsystems/`

3. **For each file in `notes/subsystems/` missing from an index**

   - Read the file
   - Generate a concise summary describing what the document covers
   - Add or update the entry in `AGENTS.md`
   - Add or update the entry in `CLAUDE.md`

4. **For each stale index entry**

   - Remove or replace the stale path so both indexes only point at `notes/subsystems/`
   - If the subsystem still exists but the filename changed, update the entry instead of dropping it

5. **For entries that exist in both**

   - Read the `notes/subsystems/` file
   - Verify the summaries in `AGENTS.md` and `CLAUDE.md` still reflect the current content
   - Update summaries if the document has changed

6. **Write the updated indexes**

   `AGENTS.md` format:

   ```markdown
   ## Design Documents

   Current design-oriented docs live in `notes/subsystems/`. Read the relevant doc for architecture context:

   | Document | Path | Summary |
   |----------|------|---------|
   | **Name** | `notes/subsystems/file.md` | One-two sentence summary |
   ```

   `CLAUDE.md` format:

   ```markdown
   ## Design Docs Index

   - `notes/subsystems/file.md` — One-line summary
   ```

   Keep entries sorted and keep wording concise.

7. **Also update `.cursorrules`**

   If `.cursorrules` exists and has a "Design Docs" or similar section, update it with the same `notes/subsystems/` index. If no such section exists, add one after the Specs Index section:

   ```markdown
   ## Design Docs Index

   - `notes/subsystems/file.md` — One-line summary
   ```

8. **Display summary**

   Show what changed:
   - New references added to `AGENTS.md`
   - New references added to `CLAUDE.md`
   - Updated summaries
   - Stale paths removed or renamed
   - No changes needed (if everything was in sync)

**Guardrails**
- Subsystem docs are written in Chinese (project convention)
- Summaries in `AGENTS.md` and `CLAUDE.md` are in English
- `notes/subsystems/` is the current truth for design-oriented docs
- Do not create new files under `docs/design/` as part of this command
- If source code for a missing subsystem doc cannot be found, skip it and report a warning
- Keep summaries concise: 1-2 sentences max in the AGENTS.md table
