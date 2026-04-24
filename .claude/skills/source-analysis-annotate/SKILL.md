---
name: source-analysis-annotate
description: Add or refine local `@source_analysis.section` comments in source files to support the repo's literate source-analysis workflow. Use when code lacks enough nearby semantic commentary for `notes/source_analysis/`, when existing analysis comments are stale or too low-level, or when the user asks to analyze code first and then improve the source-attached explanations before regenerating docs.
---

Add high-signal source-attached analysis comments that can later be extracted into `notes/source_analysis/`.

## Use This Skill For

- Files with missing or weak `@source_analysis.section` comments
- Refreshing stale source-analysis comments after a refactor
- Preparing code for `generate-source-analysis`
- Small dependency-slice analysis where one file's meaning depends on a few nearby files
- Cases where the main file cannot be explained honestly without annotating nearby helper types in other files

This skill is about comment design, not generic code commenting.

## Inputs To Read

- the target source file
- tightly related implementation/dependency files only as needed
- `notes/source_analysis/index.md`
- `scripts/extract_source_analysis.py`
- any existing generated page under `notes/source_analysis/...`

## Commenting Standard

Only add comments where they pay for themselves. Good candidates:

- a type that represents a concept boundary
- a struct/class whose name hides an important role distinction
- a helper whose value is in the invariant it protects, not the syntax it uses
- a construction path with lifecycle, ownership, or ordering constraints
- a read/write path where the data-flow meaning is not obvious from signatures alone
- compatibility shims, transitional contracts, or pass/resource routing rules

Do not comment:

- obvious getters/setters
- mechanically readable control flow
- every field or every function by default
- implementation trivia without architectural meaning

## Comment Shape

Use block comments in this exact pattern so the extractor can consume them:

```cpp
/*
@source_analysis.section Section Title
Markdown body...
*/
```

The body should:

- stay attached to a local code region
- explain semantic role, not restate syntax
- mention tradeoffs, invariants, or consequences when useful
- be understandable when extracted into Markdown

## Workflow

1. Read the target file and identify the smallest set of concepts that deserve explanation.
2. Read only the nearby files required to understand those concepts.
3. Decide the section boundaries:
   - concept/type boundary
   - lifecycle or construction path
   - data-flow or ownership path
4. Add or revise `@source_analysis.section` blocks near the code they explain.
5. Keep each block locally truthful; avoid mixing distant concepts into one block.
6. If the file cannot support a coherent analysis alone, annotate the closely related peer file too rather than forcing one giant comment.
7. Prefer explaining one reader journey across a tight file cluster over creating isolated comments that only make sense when cross-referenced manually.
8. After patching, run `python3 scripts/extract_source_analysis.py` if the target is already registered, or hand off to `generate-source-analysis` to finish the page setup.

## Heuristics For Good Sections

- One section should usually answer one reader question.
- Prefer “what concept is this encoding?” over “what does this line do?”.
- Prefer “why is this split/merged?” over “here is a list of members”.
- If an example helps, keep it tiny and conceptual.
- If the code already names the concept clearly, do not comment it again.

## Dependency Scope

Keep the analysis narrow:

- start from the requested file
- pull in only direct collaborators needed to explain meaning
- avoid wandering into full subsystem documentation
- if a helper type is required to explain a cache / descriptor / ownership / layout concept, annotate that helper type in place instead of faking completeness in the main file's comments

If the user really wants broad design exploration, use `openspec-explore` instead of turning source comments into a subsystem essay.

## Validation

- Re-read the patched source file and ask whether the comments help a reader infer architecture faster.
- Remove comments that merely paraphrase the code.
- If extraction applies, run `python3 scripts/extract_source_analysis.py` and inspect the generated Markdown for flow and redundancy.
