## Context

The remaining work is mostly process, but the process still needs a clear contract: what environment is required, what exact observations must be made, and what evidence is sufficient to call the requirement done.

## Goals / Non-Goals

**Goals:**
- Make scene-viewer manual acceptance explicit and repeatable.
- Define the exact checklist and evidence needed for closure.
- Align the checklist with the current demo features and Linux environment notes.

**Non-Goals:**
- No new demo features.
- No attempt to replace manual acceptance with headless automation entirely.

## Decisions

- Keep manual acceptance focused on observable user-facing demo behaviors already listed in REQ-019.
- Reuse current Linux/Xvfb and SDL/Vulkan environment notes where relevant.
- Treat completion evidence as part of the requirement closure path.

## Risks / Trade-offs

- [Manual acceptance still depends on a suitable display environment] → Document the environment assumptions explicitly.
- [Human-observed results can be underspecified] → Use a fixed checklist and artifact expectations.
