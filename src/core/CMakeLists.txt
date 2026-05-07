
set(CORE_LIB LX_Core CACHE INTERNAL "Core library name")

file(GLOB_RECURSE CORE_SOURCES
 "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(APPEND CORE_SOURCES
  ${CMAKE_SOURCE_DIR}/third_party/ImGuizmo/ImGuizmo.cpp
)

add_library(${CORE_LIB} ${CORE_SOURCES})

target_include_directories(${CORE_LIB} 
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/third_party/ImGuizmo
)

# target_link_libraries(${CORE_LIB} 
#   PUBLIC
#   glm::glm
# )

if(TARGET imgui)
  target_link_libraries(${CORE_LIB} PRIVATE imgui)
endif()
