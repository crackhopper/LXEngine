
set(SERVICE_LIB LX_Service CACHE INTERNAL "Service library name")

file(GLOB_RECURSE SERVICE_SOURCES
 "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
 "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)


add_library(${SERVICE_LIB} ${SERVICE_SOURCES})

target_link_libraries(${SERVICE_LIB} PRIVATE imgui)
# target_link_libraries(${SERVICE_LIB} PRIVATE ${FOUNDATION_LIB})


target_include_directories(${SERVICE_LIB} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
