---
name: curate-and-commit
description: Curate the current uncommitted worktree into a coherent commit and create the commit. Use when the user wants Codex to inspect current changes, group them into a sensible commit, write a clear commit message, and commit without interactive git flows.
---

Curate the current uncommitted worktree into a real git commit.

## When To Use

Use this skill when the user asks to:

-整理当前未提交内容并提交
- create a commit from the current worktree
- group current changes into one coherent commit
- write a commit message for the current local changes

Do not use this skill for code review, rebasing, amending old commits, or history rewriting unless the user explicitly asks for that.

## Core Rules

- Never use interactive git flows.
- Never use destructive commands such as `git reset --hard` or `git checkout --`.
- Never revert unrelated user changes.
- Treat the current worktree as potentially dirty and partially user-authored.
- If changes are clearly unrelated, do not force them into one commit without calling that out.
- Prefer one coherent commit. If the worktree actually contains multiple unrelated topics, explain that and ask before splitting only if needed. Otherwise, make the narrowest reasonable commit for the requested scope.

## Required Workflow

### 1. Inspect the worktree

Start with:

```bash
git status --short
git diff --stat
git diff --cached --stat
```

Then read focused diffs for files that appear relevant. Prefer `git diff -- <path>` and focused file reads over dumping the whole repo.

### 2. Infer the commit scope

Determine:

- what user-facing change was made
- which files are part of that change
- whether there are unrelated edits in the worktree

Use the smallest defensible scope. Do not silently absorb obvious unrelated work.

### 3. Validate before committing

Before committing, verify what matters for the scoped change:

- build or test if practical
- lint or validate if a cheap project-specific command exists
- if validation is not practical, note that explicitly

Prefer project-native checks such as:

```bash
cmake --build build
ctest --test-dir build
scripts/notes/serve_site.sh --build
```

Only run checks relevant to the scoped change.

### 4. Stage intentionally

Stage only the files that belong in the commit. Prefer explicit paths:

```bash
git add path/to/file1 path/to/file2
```

Avoid `git add .` unless the entire worktree is intentionally part of the commit and you have verified that.

### 5. Write the commit message

The message should be concrete and scoped to what actually changed.

Preferred style:

- short imperative subject
- optional body only if it adds real value

Examples:

- `sync subsystem docs with current renderer code`
- `add project skill for subsystem doc audits`
- `document material contract v2 design`

Avoid vague subjects like:

- `misc updates`
- `fix stuff`
- `changes`

### 6. Commit non-interactively

Use:

```bash
git commit -m "subject"
```

If a body is needed:

```bash
git commit -m "subject" -m "body"
```

Do not amend unless the user explicitly requests it.

## Handling Mixed Worktrees

If the worktree contains both:

- changes relevant to the user request
- clearly unrelated edits

then:

- commit only the relevant files
- leave unrelated files unstaged
- mention that the worktree still contains unrelated changes after the commit

If the user explicitly asks to commit everything, do that, but still summarize the mixed scope before committing.

## Output Expectations

When done, report:

- the commit subject
- the commit hash
- what was included
- whether any changes were intentionally left out
- what validation was run, or what was not run

If you could not safely produce a coherent commit, say why instead of forcing one.
