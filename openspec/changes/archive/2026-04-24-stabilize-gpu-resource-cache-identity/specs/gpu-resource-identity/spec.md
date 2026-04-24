## ADDED Requirements

### Requirement: CPU resources expose stable backend-cache identity
CPU-side resources synchronized into backend GPU caches SHALL expose or derive a stable identity that is not merely the current object address.

#### Scenario: destroyed object address reuse does not alias old GPU entry
- **WHEN** one CPU resource is destroyed and a later allocation reuses the same memory address
- **THEN** the backend cache does not treat the new resource as the old one solely because of pointer equality

### Requirement: Cache behavior is not coupled to one-frame liveness only
Backend GPU resource cache correctness SHALL NOT depend on resources being re-synchronized every frame just to avoid immediate destruction and recreation.

#### Scenario: temporarily unused resource can retain correct identity
- **WHEN** a synchronized resource is unused for a short period and later referenced again
- **THEN** cache correctness is preserved without relying on incidental pointer identity
