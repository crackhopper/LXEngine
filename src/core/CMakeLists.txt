set(CORE_LIB LX_Core CACHE INTERNAL "Core library name")

file(GLOB_RECURSE CORE_SOURCES
 "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(${CORE_LIB} ${CORE_SOURCES})

target_include_directories(${CORE_LIB} 
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# target_link_libraries(${CORE_LIB} 
#   PUBLIC
#   glm::glm
# )
