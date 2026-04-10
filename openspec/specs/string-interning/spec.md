## ADDED Requirements

### Requirement: GlobalStringTable provides unique ID for each string
`GlobalStringTable` SHALL maintain a global mapping from string to `uint32_t` ID. For any given string, the returned ID MUST be the same across all calls, regardless of calling thread or time.

#### Scenario: First registration of a string
- **WHEN** `getOrCreateID("u_BaseColor")` is called for the first time
- **THEN** a new unique `uint32_t` ID is returned and stored internally

#### Scenario: Repeated query of same string
- **WHEN** `getOrCreateID("u_BaseColor")` is called again after initial registration
- **THEN** the same ID as the first call is returned

#### Scenario: Different strings get different IDs
- **WHEN** `getOrCreateID("u_BaseColor")` and `getOrCreateID("u_Time")` are called
- **THEN** the two returned IDs MUST be different

### Requirement: GlobalStringTable supports reverse lookup
`GlobalStringTable` SHALL support looking up the original string from an ID, for debugging and logging purposes.

#### Scenario: Reverse lookup of registered ID
- **WHEN** `getName(id)` is called with an ID previously returned by `getOrCreateID`
- **THEN** the original string is returned

#### Scenario: Reverse lookup of unknown ID
- **WHEN** `getName(id)` is called with an ID that was never assigned
- **THEN** a fallback string (e.g. `"UNKNOWN_PROPERTY"`) is returned

### Requirement: GlobalStringTable is thread-safe
All operations on `GlobalStringTable` SHALL be safe to call from multiple threads concurrently without data races.

#### Scenario: Concurrent registration from multiple threads
- **WHEN** multiple threads call `getOrCreateID` with different strings simultaneously
- **THEN** each thread receives a correct, unique ID without data corruption

#### Scenario: Concurrent read and write
- **WHEN** one thread calls `getOrCreateID` (write) while another calls `getName` (read)
- **THEN** both operations complete correctly without data races

### Requirement: StringID wraps uint32_t with string construction
`StringID` SHALL be a struct wrapping a `uint32_t` ID. It MUST support implicit construction from `const char*` and `const std::string&` via `GlobalStringTable`. Construction from `uint32_t` MUST be `explicit`.

#### Scenario: Implicit construction from string literal
- **WHEN** a function accepting `StringID` is called with `"u_Time"`
- **THEN** a `StringID` is implicitly constructed with the ID for `"u_Time"` from `GlobalStringTable`

#### Scenario: Equality comparison
- **WHEN** two `StringID` values constructed from the same string are compared with `==`
- **THEN** the comparison returns `true`

#### Scenario: Use as unordered_map key
- **WHEN** `StringID` is used as a key in `std::unordered_map<StringID, T, StringID::Hash>`
- **THEN** it works correctly with zero hash collisions (since IDs are unique integers)

### Requirement: MakeStringID convenience function
A free function `MakeStringID(const std::string&)` SHALL be provided as a convenience wrapper around `GlobalStringTable::get().getOrCreateID()`.

#### Scenario: MakeStringID returns same ID as StringID constructor
- **WHEN** `MakeStringID("u_Color")` is called and separately `StringID("u_Color")` is constructed
- **THEN** both have the same underlying `uint32_t` ID

### Requirement: MaterialTemplate uses StringID for binding cache
`MaterialTemplate::m_bindingCache` SHALL use `StringID` as its key type instead of `std::string`.

#### Scenario: Building binding cache from shader reflection
- **WHEN** `buildBindingCache()` is called
- **THEN** each shader binding name is converted to a `StringID` and stored in the cache

#### Scenario: Finding binding by StringID
- **WHEN** `findBinding(StringID("u_BaseColor"))` is called
- **THEN** the corresponding `ShaderResourceBinding` is returned if it exists

### Requirement: MaterialInstance uses StringID for property storage
`MaterialInstance` SHALL use `StringID` as the key type for all property maps (`m_vec4s`, `m_floats`, `m_textures`). The `setVec4`, `setFloat`, and `setTexture` methods SHALL accept `StringID` as their first parameter.

#### Scenario: Setting a float property by string
- **WHEN** `mat->setFloat("u_Time", 1.0f)` is called
- **THEN** `"u_Time"` is implicitly converted to `StringID` and the value is stored under that ID
