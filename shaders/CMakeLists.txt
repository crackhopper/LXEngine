# Compile GLSL shaders to SPIR-V for Vulkan.
# VulkanShader / VulkanPipelineBase expect:
#   shaders/glsl/<shaderName>.vert.spv
#   shaders/glsl/<shaderName>.frag.spv

find_program(GLSLC_EXECUTABLE
  NAMES glslc
  HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/x86_64/bin
  REQUIRED
)

set(GLSL_INPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/glsl")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/glsl")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

file(GLOB GLSL_VERT_FILES "${GLSL_INPUT_DIR}/*.vert")
file(GLOB GLSL_FRAG_FILES "${GLSL_INPUT_DIR}/*.frag")

set(SPV_FILES "")

foreach(SHADER_FILE ${GLSL_VERT_FILES} ${GLSL_FRAG_FILES})
  get_filename_component(FILE_NAME "${SHADER_FILE}" NAME) # e.g. blinnphong_0.vert

  if (FILE_NAME MATCHES "^(.+)\\.vert$")
    set(SHADER_NAME "${CMAKE_MATCH_1}")
    set(OUTPUT_SPV "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.vert.spv")
  elseif (FILE_NAME MATCHES "^(.+)\\.frag$")
    set(SHADER_NAME "${CMAKE_MATCH_1}")
    set(OUTPUT_SPV "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.frag.spv")
  else()
    message(FATAL_ERROR "Unrecognized shader filename: ${FILE_NAME}")
  endif()

  list(APPEND SPV_FILES "${OUTPUT_SPV}")

  add_custom_command(
    OUTPUT "${OUTPUT_SPV}"
    COMMAND "${GLSLC_EXECUTABLE}"
            -o "${OUTPUT_SPV}"
            "${SHADER_FILE}"
    DEPENDS "${SHADER_FILE}"
    COMMENT "Compiling shader: ${FILE_NAME} -> ${OUTPUT_SPV}"
    VERBATIM
  )
endforeach()

add_custom_target(CompileShaders DEPENDS ${SPV_FILES})

