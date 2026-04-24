## Purpose

Define the requirement for keeping at least one concrete non-default material example in the repository and routing it through the real material asset path.

## Requirements

### Requirement: Repository includes a non-default custom material example
The repository SHALL include at least one concrete custom material example that is not `blinnphong_0` and is intended to validate the current material-asset workflow.

#### Scenario: second material example exists
- **WHEN** a developer inspects repository-owned material examples
- **THEN** at least one maintained example exists beyond `blinnphong_0`

### Requirement: Example uses the formal material asset path
The custom material example SHALL load through the current `.material` asset path and MUST NOT rely on a separate one-off initialization path.

#### Scenario: example exercises real loader flow
- **WHEN** the custom material example is consumed by test or demo code
- **THEN** it passes through the formal material asset loader contract

### Requirement: Docs reference the concrete example
Material-facing notes or tutorials SHALL reference the concrete example asset by name so readers can trace the current contract through a real artifact.

#### Scenario: documentation names the example
- **WHEN** a developer reads the material notes/tutorial path
- **THEN** the docs point to the repository-owned custom material example instead of only abstract descriptions
