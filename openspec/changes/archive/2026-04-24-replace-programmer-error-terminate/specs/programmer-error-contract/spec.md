## ADDED Requirements

### Requirement: Programmer-error validation paths throw logic errors
Engine validation paths representing invalid caller input or violated authoring contracts SHALL fail fast by throwing `std::logic_error` rather than unconditionally terminating the process.

#### Scenario: invalid authoring contract is testable
- **WHEN** a caller triggers a documented programmer-error condition such as an undefined pass or duplicate scene-node name
- **THEN** the engine throws a logic error that tests can observe directly

### Requirement: Fatal runtime failures remain separate
This contract SHALL NOT reclassify unrecoverable backend/runtime failures as logic errors.

#### Scenario: runtime fatal failure is unchanged
- **WHEN** a truly unrecoverable runtime failure occurs in backend initialization or submission
- **THEN** that path is not forced into the programmer-error exception contract by this change
