#pragma once

// REQ-018 debug UI helper. Thin wrappers over ImGui for project types so demo
// code stays short and consistent. This header must not include any ImGui
// header — ImGui stays confined to debug_ui.cpp. Callers remain free to mix
// raw ImGui calls with these helpers.

#include "core/math/vec.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"
#include "core/time/clock.hpp"
#include "core/utils/string_table.hpp"

#include <string>

namespace LX_infra::debug_ui {

// ---- Vector / scalar bridging -------------------------------------------

bool dragVec3(const char* label, LX_core::Vec3f& value,
              float speed = 0.01f, float min = 0.0f, float max = 0.0f);
bool dragVec4(const char* label, LX_core::Vec4f& value,
              float speed = 0.01f, float min = 0.0f, float max = 0.0f);

bool sliderFloat(const char* label, float& value, float min, float max);
bool sliderInt(const char* label, int& value, int min, int max);

bool colorEdit3(const char* label, LX_core::Vec3f& rgb);
bool colorEdit4(const char* label, LX_core::Vec4f& rgba);

// ---- Label / display helpers --------------------------------------------

void labelText(const char* label, const char* value);
void labelText(const char* label, const std::string& value);
void labelFloat(const char* label, float value);
void labelInt(const char* label, int value);
void labelStringId(const char* label, LX_core::StringID value);

// ---- Panel / section containers -----------------------------------------

bool beginPanel(const char* title);
void endPanel();

bool beginSection(const char* title);
void endSection();

void separatorText(const char* label);

// ---- Composite panels ---------------------------------------------------

void renderStatsPanel(const LX_core::Clock& clock);
void cameraPanel(const char* title, LX_core::CameraComponent& camera);
void directionalLightPanel(const char* title,
                           LX_core::DirectionalLight& light);

} // namespace LX_infra::debug_ui
