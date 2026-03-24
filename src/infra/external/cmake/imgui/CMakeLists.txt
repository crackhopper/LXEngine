add_library(imgui STATIC)

set(IMGUI_ROOT
  ${CMAKE_CURRENT_SOURCE_DIR}/../../imgui
)

target_sources(imgui PRIVATE
    ${IMGUI_ROOT}/imgui.cpp
    ${IMGUI_ROOT}/imgui_draw.cpp
    ${IMGUI_ROOT}/imgui_tables.cpp
    ${IMGUI_ROOT}/imgui_widgets.cpp
    ${IMGUI_ROOT}/imgui_demo.cpp
    ${IMGUI_ROOT}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_ROOT}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui
    PUBLIC
        ${IMGUI_ROOT}
        ${IMGUI_ROOT}/backends
)

target_link_libraries(imgui
    PRIVATE
        Vulkan::Vulkan
        SDL3::SDL3
)

target_compile_features(imgui PUBLIC cxx_std_17)
