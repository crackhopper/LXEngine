---
name: "Sync Design Docs"
description: Sync AGENTS.md design section with docs/design/ directory
category: Documentation
tags: [docs, design, sync]
---

Synchronize the "Design Documents" section in `AGENTS.md` with the `docs/design/` directory. Ensure both sides are consistent — missing entries are filled in by reading source code or existing docs.

**Input**: No arguments required. Run `/sync-design-docs` to perform a full sync.

**Steps**

1. **Scan both sides**

   - Read `AGENTS.md` and find the `## Design Documents` section (create it if missing, place it before `## Conventions`).
   - List all `*.md` files under `docs/design/` recursively.

2. **Diff the two sides**

   Compare the set of docs referenced in `AGENTS.md` against the files in `docs/design/`.

   Classify each entry into one of:
   - **In both**: exists in `docs/design/` AND referenced in `AGENTS.md`
   - **Missing from AGENTS.md**: file exists in `docs/design/` but no reference in `AGENTS.md`
   - **Missing from docs/design/**: referenced in `AGENTS.md` but no file in `docs/design/`

3. **For each file in `docs/design/` missing from AGENTS.md**

   - Read the file
   - Generate a 1-2 sentence summary describing what the document covers
   - Add an entry to the Design Documents table in `AGENTS.md`

4. **For each entry in AGENTS.md missing from `docs/design/`**

   - Identify the relevant subsystem from the AGENTS.md description
   - Find the corresponding source code or spec
   - Read the source/spec to understand the design
   - Create a design document in `docs/design/` with:
     - A title and overview
     - Key design decisions and data structures
     - Code examples from the actual implementation
     - Usage patterns
   - Write in Chinese (matching existing docs/design/ convention)

5. **For entries that exist in both**

   - Read the `docs/design/` file
   - Verify the summary in `AGENTS.md` still accurately reflects the document content
   - Update the summary if the document has changed

6. **Write the updated AGENTS.md**

   The Design Documents section format:

   ```markdown
   ## Design Documents

   Detailed design docs live in `docs/design/`. Read the relevant doc for architecture context:

   | Document | Path | Summary |
   |----------|------|---------|
   | **Name** | `docs/design/file.md` | One-two sentence summary |
   ```

   Keep entries sorted alphabetically by document name.

7. **Also update `.cursorrules`**

   If `.cursorrules` exists and has a "Design Docs" or similar section, update it with the same index. If no such section exists, add one after the Specs Index section:

   ```markdown
   ## Design Docs Index

   - `docs/design/file.md` — One-line summary
   ```

8. **Display summary**

   Show what changed:
   - New docs added to `docs/design/`
   - New references added to `AGENTS.md`
   - Updated summaries
   - No changes needed (if everything was in sync)

**Guardrails**
- Design docs are written in Chinese (project convention)
- Summaries in AGENTS.md are in English (AGENTS.md convention)
- Never delete files from `docs/design/` — only add or update
- Never remove entries from AGENTS.md Design Documents — only add or update
- If source code for a missing doc cannot be found, skip it and report a warning
- Keep summaries concise: 1-2 sentences max in the AGENTS.md table
