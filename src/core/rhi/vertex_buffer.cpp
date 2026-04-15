#include "vertex_buffer.hpp"
namespace LX_core {

// 内部使用的宏，不需要 inline
#define REGISTER_VERTEX_INTERNAL(VType) \
    static bool VType##_registered = []() { \
        VertexFactory::registerType<VType>(); \
        return true; \
    }();

// 在这里集中注册
REGISTER_VERTEX_INTERNAL(VertexPos);
REGISTER_VERTEX_INTERNAL(VertexPosColor);
REGISTER_VERTEX_INTERNAL(VertexPosUV);
REGISTER_VERTEX_INTERNAL(VertexPBR);
REGISTER_VERTEX_INTERNAL(VertexSkinned);
REGISTER_VERTEX_INTERNAL(VertexUI);

} // namespace LX_core