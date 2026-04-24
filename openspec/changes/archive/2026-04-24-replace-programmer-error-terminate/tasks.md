## 1. Contract Classification

- [x] 1.1 Confirm the identified `std::terminate()` call sites are programmer-error contracts rather than fatal runtime failures.
- [x] 1.2 Introduce a consistent logic-error throw path for those contracts.

## 2. Call Site Migration

- [x] 2.1 Replace the scene/material/material-loader programmer-error termination sites with logic errors.
- [x] 2.2 Update focused tests to assert the new failure behavior where practical.

## 3. Verification

- [x] 3.1 Build and run the affected scene/material tests.
- [x] 3.2 Confirm no true backend-fatal path was accidentally softened in this migration.
