## ADDED Requirements

### Requirement: Texture loading from file
The texture loading system SHALL provide a `TextureLoader` class capable of loading image files into GPU-friendly formats.

#### Scenario: Load PNG texture
- **WHEN** `TextureLoader::load("texture.png")` is called on a valid PNG file
- **THEN** the file SHALL be read, decoded by stb_image, and stored as RGBA data

#### Scenario: Load JPEG texture
- **WHEN** `TextureLoader::load("texture.jpg")` is called on a valid JPEG file
- **THEN** the file SHALL be read, decoded by stb_image, and stored as RGBA data

#### Scenario: File not found
- **WHEN** `load()` is called with a non-existent file path
- **THEN** a `std::runtime_error` SHALL be thrown with an appropriate message

### Requirement: Texture data access
The texture loader SHALL provide access to loaded texture metadata and pixel data.

#### Scenario: Query texture dimensions
- **WHEN** `getWidth()` or `getHeight()` is called after loading
- **THEN** the pixel width and height of the texture SHALL be returned

#### Scenario: Access raw pixel data
- **WHEN** `getData()` is called after loading
- **THEN** a pointer to the raw RGBA pixel data SHALL be returned

### Requirement: Texture format conversion
The texture loader SHALL convert all loaded images to RGBA format for consistent GPU upload.

#### Scenario: Grayscale to RGBA conversion
- **WHEN** a grayscale image is loaded
- **THEN** it SHALL be converted to 4-channel RGBA with all channels set to the original grayscale value and alpha set to 255
