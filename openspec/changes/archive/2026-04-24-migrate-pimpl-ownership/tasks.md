## 1. Ownership Migration

- [x] 1.1 Replace raw owning pImpl fields with `std::unique_ptr` in the remaining backend/infra classes named by the review.
- [x] 1.2 Remove corresponding manual `new/delete` code and keep destructors in valid translation units.

## 2. Pattern Consistency

- [x] 2.1 Apply the same pImpl ownership pattern across window, GUI, and loader implementations still using raw owning pointers.
- [x] 2.2 Update nearby comments or docs where they still imply manual ownership.

## 3. Verification

- [x] 3.1 Build the affected targets after the ownership migration.
- [x] 3.2 Run focused smoke tests covering window, renderer, and loader initialization paths.
