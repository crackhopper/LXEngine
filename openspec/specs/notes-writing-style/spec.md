# Notes Writing Style

## Purpose

Define the house style for human-facing documentation under `notes/`, especially the `concepts/` section.

This spec is not about code behavior. It is about how we explain the engine to human readers so that notes stay readable, consistent, and aligned with the project voice.

## Scope

This spec applies to:

- `notes/concepts/`
- `notes/tutorial/`
- user-facing explanatory sections inside `notes/subsystems/` when they link back to concepts

It does not replace technical specs under `openspec/specs/`, which remain normative for implementation behavior.

## Requirements

### Requirement: Concepts use an explanatory narrative, not a reference-manual voice

Concept documents SHALL read like guided explanations, not API reference pages or product manuals.

They SHALL answer, in natural reading order:

- what this system is in the current engine
- what problem it solves
- how it is typically used in the current codebase
- where its boundaries are
- how it connects to the implementation and deeper docs

Concept documents SHALL avoid template-like headings such as `这个系统是什么` / `它有什么用` / `底层现在怎么实现` when used mechanically across every page.

Instead, headings SHOULD be phrased as meaningful reading cues that match the topic, for example:

- `模板与 Pass：材质的结构定义`
- `Shader 在材质中的角色`
- `模板如何影响 Pipeline`

#### Scenario: Page headings are topic-specific
- **WHEN** a new concept page is created
- **THEN** its title and section headings describe the mechanism or role being explained, not a generic category label

### Requirement: Use analogies to anchor abstract concepts

Concept pages SHALL introduce core abstractions with a concrete analogy before any technical detail.

The analogy SHOULD:

- map the abstract object to something the reader already understands
- be stated once at the introduction and reused consistently across the page
- appear in tables and inline when it aids understanding

Example: "`MaterialTemplate` is a recipe — it defines what steps (passes) a dish needs and what kind of ingredients (shader / variants / render state). `MaterialInstance` is the dish on the table — filled with actual seasoning amounts (parameter values) and real ingredients (textures)."

The analogy MUST NOT replace technical precision. After the analogy, the document SHALL ground each point in concrete code objects, field names, and API calls.

#### Scenario: Abstract concept introduced with analogy
- **WHEN** a concept page introduces `MaterialPassDefinition`
- **THEN** it first states a one-sentence analogy (e.g., "a single step in the recipe"), then immediately maps its fields to concrete meanings via a table or list

### Requirement: Use tables for structured comparisons

Concept pages SHALL use Markdown tables when a concept involves:

- multiple objects with parallel attributes
- a boundary between two systems (e.g., template vs instance)
- a set of fields with name / meaning / analogy

the document SHALL use a Markdown table rather than prose paragraphs or flat bullet lists.

Tables SHOULD have 2–4 columns. Common patterns:

- `| Object | Role | Analogy |`
- `| Field | Meaning | Analogy |`
- `| Affects pipeline | Does not affect pipeline |`
- `| Path | When to use |`

#### Scenario: Template vs instance boundary
- **WHEN** a page needs to explain what belongs to template vs instance
- **THEN** it presents a two-column table, not interleaved prose

### Requirement: Show YAML-to-code correspondence for configurable concepts

When a concept has a YAML configuration surface (e.g., `.mat.yaml`), the document SHALL include at least one annotated YAML block showing the correspondence between YAML fields and runtime objects.

Annotations SHOULD be inline comments mapping YAML keys to C++ types or API calls:

```yaml
passes:
  Forward:                          # → template.setPass(Pass_Forward, ...)
    renderState:                    # → MaterialPassDefinition.renderState
      cullMode: Back
```

This helps the reader build a mental model that spans both the data format and the runtime representation.

#### Scenario: Configurable concept includes YAML mapping
- **WHEN** a concept page describes a system that can be configured via YAML
- **THEN** the page includes at least one YAML block with inline `# →` annotations linking to the runtime equivalents

### Requirement: Concepts describe only current reality

Concept pages SHALL describe the current engine as it exists today.

They SHALL NOT:

- reference old designs that no longer exist (e.g., "no longer uses X", "previously had Y")
- compare against a removed implementation to explain the current one
- present future architecture as if it already exists

When a capability is incomplete or missing:

- the document SHALL clearly label it as partial or not yet implemented
- the document SHALL link to an active requirement when one exists

#### Scenario: No references to removed designs
- **WHEN** reviewing a concept page
- **THEN** it contains no phrases like "不再维护", "已经去掉", "旧的做法是" — only forward-facing description of current behavior

### Requirement: Use "我们" voice, not second-person instruction voice

Human-facing notes SHALL use first-person plural narration (`我们`) as the default voice.

They SHALL NOT use second-person instructional voice (`你`) as the primary narrative style.

Short direct instructions are allowed inside code snippets, warnings, or migration notes, but surrounding prose SHALL remain in `我们` voice.

#### Scenario: Page uses correct voice
- **WHEN** a concept page is reviewed
- **THEN** the primary narrative uses `我们` and avoids `你会` / `你可以` / `如果你想` patterns

### Requirement: Key API surfaces presented as compact tables

When a page mentions more than 3 API methods or fields, it SHALL use a table with `| Method | Purpose |` or `| Field | Meaning |` format rather than an enumerated list of single-sentence explanations.

#### Scenario: API surface in table form
- **WHEN** a concept page lists MaterialTemplate's key methods
- **THEN** they appear in a `| Method | Purpose |` table, not a bullet list

### Requirement: Cross-links preserve the concept-to-design ladder

Concept pages SHALL link readers downward into deeper implementation documents.

The usual ladder is:

1. concept page explains the role and usage
2. subsystem page explains current implementation shape
3. spec explains normative behavior

Concept pages SHOULD end with a "继续阅读" section containing 2–4 links to the most relevant subsystem docs, source files, or specs.

#### Scenario: Page ends with focused cross-links
- **WHEN** a concept page is complete
- **THEN** it ends with a short list of 2–4 links to deeper docs, not a long reference dump

### Requirement: Index pages use a consistent structure

Index pages (e.g., `index.md` for a concept group) SHALL follow this structure:

1. **One-sentence system description** — what it does in the engine
2. **One-paragraph analogy** — anchor the reader's mental model
3. **Core objects table** — key types with role and analogy columns
4. **Reading order** — numbered list of sub-pages with descriptive titles
5. **Authority references** — links to subsystem docs and specs

#### Scenario: Index page structure
- **WHEN** a new concept group index is created
- **THEN** it contains all five sections in the order above

### Requirement: Navigation labels SHALL stay concise when page titles are richer

Navigation labels in `notes/nav.yml` SHALL stay short and scannable, and MAY use simple capability names (e.g., `材质系统`). The page title itself SHOULD be more expressive and explanatory.

#### Scenario: Nav label vs page title
- **WHEN** a page is added to navigation
- **THEN** the nav label is short for scanning, while the page title is descriptive for understanding

## Acceptance Criteria

A concept page is compliant with this spec if:

- its primary narrative voice is `我们`, not `你`
- abstract concepts are introduced with a concrete analogy before technical detail
- structured comparisons use tables, not interleaved prose
- configurable concepts include annotated YAML blocks showing runtime correspondence
- it describes only current behavior with no references to removed designs
- API surfaces with 3+ entries are presented as tables
- it ends with 2–4 focused cross-links, not a reference dump
- its title helps a reader understand the topic, not just classify it
