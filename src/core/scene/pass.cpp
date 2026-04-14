#include "core/scene/pass.hpp"

#include "core/gpu/render_resource.hpp"

namespace LX_core {

ResourcePassFlag passFlagFromStringID(StringID pass) {
  if (pass == Pass_Forward)
    return ResourcePassFlag::Forward;
  if (pass == Pass_Deferred)
    return ResourcePassFlag::Deferred;
  if (pass == Pass_Shadow)
    return ResourcePassFlag::Shadow;
  return static_cast<ResourcePassFlag>(0);
}

} // namespace LX_core
