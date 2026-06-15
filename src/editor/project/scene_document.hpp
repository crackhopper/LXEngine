#pragma once

#include "infra/scene_io/scene_document.hpp"

namespace LX_demo::lxe_editor {

using CameraNodeState = LX_infra::scene_io::CameraNodeState;
using EditorCameraState = LX_infra::scene_io::EditorCameraState;
using EnvironmentState = LX_infra::scene_io::EnvironmentState;
using LightKind = LX_infra::scene_io::LightKind;
using LightNodeState = LX_infra::scene_io::LightNodeState;
using MaterialOverrideState = LX_infra::scene_io::MaterialOverrideState;
using ProceduralMaterialState = LX_infra::scene_io::ProceduralMaterialState;
using SceneDocument = LX_infra::scene_io::SceneDocument;
using SceneNodeDocument = LX_infra::scene_io::SceneNodeDocument;

using LX_infra::scene_io::loadSceneDocument;
using LX_infra::scene_io::saveSceneDocument;

} // namespace LX_demo::lxe_editor
