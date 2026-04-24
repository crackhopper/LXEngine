## ADDED Requirements

### Requirement: Owning pImpl fields use RAII smart pointers
Classes that own implementation objects through the pImpl pattern SHALL represent that ownership with `std::unique_ptr` rather than raw owning pointers and manual `new/delete`.

#### Scenario: pImpl ownership is explicit
- **WHEN** a class owns an implementation object behind a forward declaration
- **THEN** the owning field is a smart pointer and destruction follows RAII rather than manual delete logic

### Requirement: pImpl destruction remains valid with incomplete types
If a pImpl-owning class requires out-of-line destruction for incomplete-type correctness, the implementation SHALL define that destructor in a translation unit that sees the full implementation type.

#### Scenario: unique_ptr pImpl compiles cleanly
- **WHEN** a class stores `std::unique_ptr<Impl>`
- **THEN** its destructor placement respects incomplete-type rules and does not rely on undefined behavior or ad hoc workarounds
