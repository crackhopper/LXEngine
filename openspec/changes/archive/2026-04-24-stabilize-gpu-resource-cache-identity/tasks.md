## 1. Identity Contract

- [x] 1.1 Define the stable identity source used by CPU resources synchronized into the Vulkan resource cache.
- [x] 1.2 Remove raw object-address identity as the primary cache key contract.

## 2. Cache/Lifetime Behavior

- [x] 2.1 Update resource-manager lookup and GC logic to use the new identity contract.
- [x] 2.2 Validate behavior for temporarily unused resources and resource recreation.

## 3. Verification

- [x] 3.1 Add focused tests or instrumentation covering cache reuse and address-reuse safety.
- [x] 3.2 Run the relevant Vulkan resource-manager tests after the change.
