---
name: generate-source-analysis
description: Generate or update `notes/source_analysis/` pages using the repo's literate source-analysis workflow. Trigger this when the user asks for `generate_source_analysis`, asks to更新/生成源码分析文档, refreshes a page under `notes/source_analysis/`, or wants source-attached analysis comments extracted into Markdown. If the target code lacks enough local `@source_analysis.section` comments, first use the helper skill `source-analysis-annotate`, then continue the document generation flow transparently.
---

Generate or refresh `notes/source_analysis/` pages from source-attached analysis comments.

## User-Facing Contract

Treat `generate_source_analysis` as the main entrypoint.

The user should not need to care whether the workflow:

- directly regenerates a page from existing comments, or
- first improves source comments and then regenerates the page

That branching is internal to this skill.

## Modes

Treat `generate_source_analysis` as covering two concrete modes:

### 1. New Page

Use when the target source file or source cluster does not yet have a corresponding page under `notes/source_analysis/`.

Expected outcomes:

- source-attached `@source_analysis.section` comments exist where needed, including nearby dependency files when the concept crosses file boundaries
- a new target is registered in `scripts/extract_source_analysis.py`
- a new generated page exists under the mirrored `notes/source_analysis/...` path
- the page has a sensible manual `SOURCE_ANALYSIS:EXTRA` section

### 2. Update Existing Page

Use when the target page already exists and needs to catch up with code changes, refactors, renamed concepts, or improved explanations.

Expected outcomes:

- stale source-analysis comments are repaired only where needed
- the extracted page matches the current code again
- the manual extra section is refreshed without losing valid prose

## Repo Contract

- Source comments live close to code in `/* ... */` blocks beginning with `@source_analysis.section Title`.
- Extraction is performed by `python3 scripts/extract_source_analysis.py`.
- Generated pages mirror a primary source path under `notes/source_analysis/`, but one page may aggregate multiple tightly related source files.
- Manual synthesis lives after `<!-- SOURCE_ANALYSIS:EXTRA -->` and is preserved by the extractor.
- Extraction targets are explicitly listed in `scripts/extract_source_analysis.py`.

Read these first:

- `notes/source_analysis/index.md`
- `scripts/extract_source_analysis.py`
- the target source file(s)
- the existing generated page, if any

## Workflow

1. Identify the target source file and the minimum tightly related file group required to explain it correctly.
2. Determine mode:
   - new page
   - update existing page
3. Inspect whether existing `@source_analysis.section` comments are sufficient to support a good page.
4. If comments are insufficient, stale, trivial, or too low-level:
   - use the helper skill `source-analysis-annotate`
   - improve only the comments that clarify concept boundaries, lifecycle, invariants, data flow, or tradeoffs
   - annotate nearby dependency types when the requested concept cannot be understood honestly without them
5. If this is a new page:
   - add a target entry to `scripts/extract_source_analysis.py`
   - choose one primary source path to mirror under `notes/source_analysis/`
   - register any additional source files that should be extracted into the same page
   - write a short intro that frames the reading angle
6. If this is an existing page:
   - read the current generated page
   - keep valid manual prose after `SOURCE_ANALYSIS:EXTRA`
   - remove stale claims, old names, and outdated comparisons
7. Run `python3 scripts/extract_source_analysis.py`.
8. Refine the `SOURCE_ANALYSIS:EXTRA` section for cross-section synthesis, comparisons, reading guidance, and broader context.
9. If source-analysis navigation ordering is affected, update the ordering metadata and generated nav integration rather than only editing a one-off page list.
10. Update related nav or nearby notes only when clearly necessary.

## New Page Rules

When creating a brand-new source-analysis page:

- choose the output path by mirroring the primary source path under `notes/source_analysis/`
- keep the page focused on one source file or one very tight file cluster
- write an intro that answers:
  - why this file or cluster is worth reading
  - what reader question should frame the page
  - what nearby concepts it connects to
- do not add a target for a file that still has no meaningful analysis comments unless you are also fixing that in the same run
- prefer one concept entry page over multiple thin pages when a reader would otherwise bounce between helper types just to understand the main file

### Adding A Target

When updating `scripts/extract_source_analysis.py`:

- add one explicit `SourceAnalysisTarget`
- keep the primary source, output, title, intro, and any additional related sources all concrete
- do not turn the script into a repo-wide auto-discovery system unless the user explicitly asks for that design change
- keep the output path stable once introduced, unless a source file move genuinely requires changing it
- if one page aggregates multiple files, keep the aggregation narrow and intentional; do not turn a page into a whole-subsystem dump

Title guidance:

- prefer concept-led titles, not raw filenames
- reflect what the reader will understand after reading
- avoid generic titles like “Some Utilities” or “Implementation Notes”

Intro guidance:

- 1 short block is enough
- explain reading angle and design question
- avoid repeating the page title in different words

## Existing Page Update Rules

When updating an existing page:

- treat the source as truth, not the old Markdown
- preserve useful manual synthesis, but rewrite stale claims aggressively
- if extracted sections changed shape, rewrite the extra section so it still matches the new structure
- remove commentary that refers to deleted helper types, old ownership models, or obsolete layering

Typical update triggers:

- type merge/split
- rename of a concept
- ownership/lifecycle changes
- pass/data-flow changes
- comments that became misleading after refactor

## `SOURCE_ANALYSIS:EXTRA` Rules

Use the manual tail only for material that should not live in code comments:

- cross-section synthesis
- construction order walkthroughs
- lifecycle or ownership caveats spanning multiple sections
- comparisons with old designs
- “recommended reading order” guidance

Do not use the extra section for:

- repeating the extracted section bodies
- low-level paraphrase of code
- broad subsystem documentation that belongs in `notes/subsystems/`

When editing the extra section:

- preserve any still-correct manual prose
- delete stale prose rather than leaving historical residue
- prefer a few coherent sections over many tiny fragments

## Writing Rules

For source comments:

- keep them local to the code they explain
- explain semantics and role boundaries, not syntax line-by-line
- prefer concept-level commentary over low-level paraphrase
- add examples only when they materially clarify meaning
- do not blanket-comment obvious code

For generated Markdown:

- use the extracted sections as the backbone
- keep the extra section for broader synthesis that should not live in code
- preserve the repo's Chinese prose style
- keep the page aligned with the source path layout

## Decision Heuristic

Use this quick decision rule:

- if the page exists and the source comments are still structurally good, update
- if the page exists but the source comments no longer explain the current design, annotate then update
- if the page does not exist, annotate if needed, register target, then generate
- if the target concept depends on helper types in nearby files, annotate those files too and aggregate them into one page instead of creating fragmented single-file pages by default
- if the request is really about broad architecture rather than source-attached reading, do not force it into this workflow

## Validation

- run `python3 scripts/extract_source_analysis.py`
- verify the generated page renders the intended sections in order
- verify aggregated pages clearly show which source file each extracted section came from
- confirm the `SOURCE_ANALYSIS:EXTRA` tail was preserved
- for new pages, verify the target path and intro are correct
- for updated pages, verify stale statements were actually removed
- if nav ordering metadata changed, verify the generated notes nav reflects the new order
- if semantics changed while annotating, run the smallest relevant build/test command when feasible

## Escalation Rules

If the work drifts into broad subsystem design rather than source-attached explanation, switch mental mode and use `openspec-explore` instead of forcing that content into `notes/source_analysis/`.
