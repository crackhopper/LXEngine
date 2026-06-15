#include "core/asset/material_contract_packer.hpp"

#include "core/asset/material_instance.hpp"

#include <cstring>

namespace LX_core {

namespace {

[[nodiscard]] u32 alignTo(u32 value, u32 alignment) {
  if (alignment == 0) {
    return value;
  }
  const u32 remainder = value % alignment;
  return remainder == 0 ? value : value + (alignment - remainder);
}

[[nodiscard]] u32 fieldAlignment(MaterialContractStorageFieldType type) {
  switch (type) {
  case MaterialContractStorageFieldType::Vec4:
    return 16;
  case MaterialContractStorageFieldType::Float:
  case MaterialContractStorageFieldType::TextureSlot:
  case MaterialContractStorageFieldType::ChannelSelector:
  case MaterialContractStorageFieldType::Flags:
    return 4;
  }
  return 4;
}

[[nodiscard]] u32 fieldSize(MaterialContractStorageFieldType type) {
  switch (type) {
  case MaterialContractStorageFieldType::Vec4:
    return 16;
  case MaterialContractStorageFieldType::Float:
  case MaterialContractStorageFieldType::TextureSlot:
  case MaterialContractStorageFieldType::ChannelSelector:
  case MaterialContractStorageFieldType::Flags:
    return 4;
  }
  return 4;
}

void requireDefaultTextureSlot(u32 slot, const char *name,
                               std::vector<std::string> &diagnostics) {
  if (slot == u32_max) {
    diagnostics.push_back(std::string("missing default texture slot: ") + name);
  }
}

[[nodiscard]] u32
channelSelectorCode(const std::string &channel,
                    std::vector<std::string> &diagnostics) {
  if (channel == "r") {
    return 0;
  }
  if (channel == "g") {
    return 1;
  }
  if (channel == "b") {
    return 2;
  }
  if (channel == "a") {
    return 3;
  }
  if (channel == "rgb") {
    return 4;
  }
  if (channel == "rgba") {
    return 5;
  }
  diagnostics.push_back("unknown material storage channel selector: " +
                        channel);
  return 0;
}

[[nodiscard]] u32 defaultTextureSlotForSemantic(
    const MaterialContractDefaultTextureSlots &defaults,
    const std::string &semantic,
    std::vector<std::string> &diagnostics) {
  if (semantic == "white") {
    return defaults.white;
  }
  if (semantic == "black") {
    return defaults.black;
  }
  if (semantic == "flatNormal") {
    return defaults.flatNormal;
  }
  diagnostics.push_back("unknown default texture semantic: " + semantic);
  return u32_max;
}

[[nodiscard]] std::optional<
    std::reference_wrapper<const MaterialParameterEnvelope>>
findEnvelope(const MaterialContractPackInput &input,
             const MaterialContractStorageField &field) {
  if (input.material == nullptr || field.parameterName.empty()) {
    return std::nullopt;
  }
  return input.material->getMaterialEnvelope(StringID(field.parameterName));
}

[[nodiscard]] u32 flagsForField(const MaterialContractPackInput &input,
                                const MaterialContractStorageField &field,
                                std::vector<std::string> &diagnostics) {
  u32 flags = static_cast<u32>(field.defaultValue.x);
  const auto envelope = findEnvelope(input, field);
  if (!envelope.has_value()) {
    return flags;
  }

  const MaterialParameterEnvelope &parameter = envelope->get();
  if (parameter.integerValue.has_value()) {
    return static_cast<u32>(*parameter.integerValue);
  }
  if (parameter.boolValue.has_value()) {
    return *parameter.boolValue ? 1u : 0u;
  }
  if (parameter.stringValue.has_value()) {
    const std::string &value = *parameter.stringValue;
    if (field.parameterName == "alphaMode") {
      if (value == "OPAQUE") {
        return 0u;
      }
      if (value == "MASK") {
        return 1u;
      }
      if (value == "BLEND") {
        return 2u;
      }
    }
    diagnostics.push_back("unknown flags value for material field '" +
                          field.name + "': " + value);
  }
  return flags;
}

[[nodiscard]] Vec4f
valueForField(const MaterialContractPackInput &input,
              const MaterialContractStorageField &field) {
  Vec4f value = field.defaultValue;
  const auto envelope = findEnvelope(input, field);
  if (!envelope.has_value()) {
    return value;
  }
  const MaterialParameterEnvelope &parameter = envelope->get();
  if (parameter.rgbValue.has_value()) {
    return Vec4f{parameter.rgbValue->x, parameter.rgbValue->y,
                 parameter.rgbValue->z, 1.0f};
  }
  if (parameter.floatValue.has_value()) {
    value.x = *parameter.floatValue;
  }
  return value;
}

[[nodiscard]] u32
textureSlotForField(const MaterialContractPackInput &input,
                    const MaterialContractStorageField &field,
                    std::vector<std::string> &diagnostics) {
  const auto envelope = findEnvelope(input, field);
  if (envelope.has_value() &&
      envelope->get().kind == MaterialEnvelopeKind::Texture) {
    if (input.textureSlotForParameter && !field.parameterName.empty()) {
      const u32 slot = input.textureSlotForParameter(field.parameterName);
      if (slot != u32_max) {
        return slot;
      }
    }

    if (!envelope->get().uri.has_value()) {
      diagnostics.push_back("missing texture resource for material field '" +
                            field.name + "'");
      return u32_max;
    }

    if (!input.textureSlotForUri) {
      diagnostics.push_back("texture slot resolver is required for material "
                            "texture field '" +
                            field.name + "'");
      return u32_max;
    }

    const u32 slot =
        input.textureSlotForUri(ResourceUri(*envelope->get().uri));
    if (slot == u32_max) {
      diagnostics.push_back("unresolved texture slot for material field '" +
                            field.name + "' uri '" + *envelope->get().uri +
                            "'");
    }
    return slot;
  }

  return defaultTextureSlotForSemantic(input.defaultTextureSlots,
                                       field.defaultTextureSemantic,
                                       diagnostics);
}

void writeBytes(std::vector<u8> &bytes, u32 offset, const void *data,
                u32 size) {
  const usize requiredSize = static_cast<usize>(offset) + size;
  if (bytes.size() < requiredSize) {
    bytes.resize(requiredSize, 0);
  }
  std::memcpy(bytes.data() + offset, data, size);
}

} // namespace

std::optional<std::reference_wrapper<const SourceLocalMaterialFieldLayout>>
SourceLocalMaterialRecordLayout::findField(std::string_view name) const {
  for (const SourceLocalMaterialFieldLayout &field : fields) {
    if (field.name == name) {
      return std::cref(field);
    }
  }
  return std::nullopt;
}

MaterialContractPackResult
packMaterialContractRecord(const MaterialContractPackInput &input) {
  MaterialContractPackResult result;
  result.record.sourceLocalMaterialIndex = input.sourceLocalMaterialIndex;

  requireDefaultTextureSlot(input.defaultTextureSlots.white, "white",
                            result.diagnostics);
  requireDefaultTextureSlot(input.defaultTextureSlots.black, "black",
                            result.diagnostics);
  requireDefaultTextureSlot(input.defaultTextureSlots.flatNormal, "flatNormal",
                            result.diagnostics);

  if (input.sourceLocalMaterialIndex == u32_max) {
    result.diagnostics.push_back("missing source-local material index");
  }

  u32 cursor = 0;
  for (const MaterialContractStorageField &field :
       input.contract.storageFields) {
    const u32 offset = alignTo(cursor, fieldAlignment(field.type));
    const u32 size = fieldSize(field.type);
    result.layout.fields.push_back(SourceLocalMaterialFieldLayout{
        .name = field.name,
        .type = field.type,
        .offset = offset,
        .size = size,
    });

    switch (field.type) {
    case MaterialContractStorageFieldType::Float: {
      const Vec4f value = valueForField(input, field);
      writeBytes(result.record.bytes, offset, &value.x, sizeof(value.x));
      break;
    }
    case MaterialContractStorageFieldType::Vec4: {
      const Vec4f value = valueForField(input, field);
      writeBytes(result.record.bytes, offset, &value, sizeof(value));
      break;
    }
    case MaterialContractStorageFieldType::TextureSlot: {
      const u32 slot = textureSlotForField(input, field, result.diagnostics);
      writeBytes(result.record.bytes, offset, &slot, sizeof(slot));
      break;
    }
    case MaterialContractStorageFieldType::ChannelSelector: {
      const u32 channel =
          channelSelectorCode(field.defaultChannel, result.diagnostics);
      writeBytes(result.record.bytes, offset, &channel, sizeof(channel));
      break;
    }
    case MaterialContractStorageFieldType::Flags: {
      const u32 flags = flagsForField(input, field, result.diagnostics);
      writeBytes(result.record.bytes, offset, &flags, sizeof(flags));
      break;
    }
    }

    cursor = offset + size;
  }

  result.layout.byteSize = alignTo(cursor, 16);
  result.record.bytes.resize(result.layout.byteSize, 0);
  return result;
}

} // namespace LX_core
