# Compile GLSL shaders to SPIR-V for Vulkan.
# Runtime asset layout:
#   assets/shaders/glsl/render_paths/Forward/<shaderName>.vert
#   assets/shaders/glsl/render_paths/Forward/<shaderName>.frag
#   <build>/assets/shaders/glsl/render_paths/Forward/<shaderName>.vert.spv
#   <build>/assets/shaders/glsl/render_paths/Forward/<shaderName>.frag.spv

if(POLICY CMP0116)
  cmake_policy(SET CMP0116 NEW)
endif()

find_program(GLSLC_EXECUTABLE
  NAMES glslc
  HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/x86_64/bin
  REQUIRED
)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(GLSL_INPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/glsl")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/assets/shaders/glsl")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")
get_filename_component(GLSL_INPUT_DIR_REAL "${GLSL_INPUT_DIR}" REALPATH)
get_filename_component(SHADER_OUTPUT_DIR_REAL "${SHADER_OUTPUT_DIR}" REALPATH)
if(NOT GLSL_INPUT_DIR_REAL STREQUAL SHADER_OUTPUT_DIR_REAL)
  file(COPY "${GLSL_INPUT_DIR}/" DESTINATION "${SHADER_OUTPUT_DIR}")
endif()

file(GLOB_RECURSE GLSL_RUNTIME_SOURCE_FILES CONFIGURE_DEPENDS
  "${GLSL_INPUT_DIR}/*"
)
add_custom_target(SyncShaderRuntimeSources
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUTPUT_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${GLSL_INPUT_DIR}" "${SHADER_OUTPUT_DIR}"
  DEPENDS ${GLSL_RUNTIME_SOURCE_FILES}
  COMMENT "Syncing shader runtime sources"
  VERBATIM
)

file(GLOB_RECURSE GLSL_VERT_FILES CONFIGURE_DEPENDS "${GLSL_INPUT_DIR}/*.vert")
file(GLOB_RECURSE GLSL_FRAG_FILES CONFIGURE_DEPENDS "${GLSL_INPUT_DIR}/*.frag")
file(GLOB_RECURSE GLSL_COMP_FILES CONFIGURE_DEPENDS "${GLSL_INPUT_DIR}/*.comp")

set(SPV_FILES "")
set(MATERIAL_SOURCE_VARIANT_SPV_FILES "")
set(LX_MATERIAL_SOURCE_VARIANT_CONTRACT "common/materials/standard_pbr.contract.glsl")

foreach(SHADER_FILE ${GLSL_VERT_FILES} ${GLSL_FRAG_FILES} ${GLSL_COMP_FILES})
  get_filename_component(FILE_NAME "${SHADER_FILE}" NAME)
  file(RELATIVE_PATH RELATIVE_SHADER_FILE "${GLSL_INPUT_DIR}" "${SHADER_FILE}")
  get_filename_component(RELATIVE_SHADER_DIR "${RELATIVE_SHADER_FILE}" DIRECTORY)
  file(READ "${SHADER_FILE}" SHADER_SOURCE_TEXT)
  string(FIND "${SHADER_SOURCE_TEXT}" "LX_MATERIAL_CONTRACT_SOURCE"
    SHADER_CONTRACT_SOURCE_HOOK)

  if(FILE_NAME MATCHES "^(.+)\\.vert$")
    set(SHADER_NAME "${CMAKE_MATCH_1}")
    set(OUTPUT_SPV "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.vert.spv")
  elseif(FILE_NAME MATCHES "^(.+)\\.frag$")
    set(SHADER_NAME "${CMAKE_MATCH_1}")
    set(OUTPUT_SPV "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.frag.spv")
  elseif(FILE_NAME MATCHES "^(.+)\\.comp$")
    set(SHADER_NAME "${CMAKE_MATCH_1}")
    set(OUTPUT_SPV "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.comp.spv")
  else()
    message(FATAL_ERROR "Unrecognized shader filename: ${FILE_NAME}")
  endif()
  if(NOT RELATIVE_SHADER_DIR STREQUAL "")
    if(FILE_NAME MATCHES "^(.+)\\.vert$")
      set(OUTPUT_SPV "${SHADER_OUTPUT_DIR}/${RELATIVE_SHADER_DIR}/${SHADER_NAME}.vert.spv")
    elseif(FILE_NAME MATCHES "^(.+)\\.frag$")
      set(OUTPUT_SPV "${SHADER_OUTPUT_DIR}/${RELATIVE_SHADER_DIR}/${SHADER_NAME}.frag.spv")
    elseif(FILE_NAME MATCHES "^(.+)\\.comp$")
      set(OUTPUT_SPV "${SHADER_OUTPUT_DIR}/${RELATIVE_SHADER_DIR}/${SHADER_NAME}.comp.spv")
    endif()
  endif()

  if(NOT SHADER_CONTRACT_SOURCE_HOOK EQUAL -1)
    set(VARIANT_OUTPUT_SPV "${OUTPUT_SPV}")
    if(FILE_NAME MATCHES "^(.+)\\.vert$")
      set(VARIANT_OUTPUT_SPV "${SHADER_OUTPUT_DIR}/${RELATIVE_SHADER_DIR}/${SHADER_NAME}.standard_pbr.vert.spv")
    elseif(FILE_NAME MATCHES "^(.+)\\.frag$")
      set(VARIANT_OUTPUT_SPV "${SHADER_OUTPUT_DIR}/${RELATIVE_SHADER_DIR}/${SHADER_NAME}.standard_pbr.frag.spv")
    elseif(FILE_NAME MATCHES "^(.+)\\.comp$")
      set(VARIANT_OUTPUT_SPV "${SHADER_OUTPUT_DIR}/${RELATIVE_SHADER_DIR}/${SHADER_NAME}.standard_pbr.comp.spv")
    endif()

    list(APPEND MATERIAL_SOURCE_VARIANT_SPV_FILES "${VARIANT_OUTPUT_SPV}")
    set(VARIANT_DEPFILE "${CMAKE_BINARY_DIR}/shader_deps/${RELATIVE_SHADER_FILE}.standard_pbr.spv.d")
    set(VARIANT_GENERATED_SOURCE
      "${CMAKE_BINARY_DIR}/shader_variants/standard_pbr/${RELATIVE_SHADER_FILE}")
    get_filename_component(VARIANT_OUTPUT_DIR "${VARIANT_OUTPUT_SPV}" DIRECTORY)
    get_filename_component(VARIANT_DEPFILE_DIR "${VARIANT_DEPFILE}" DIRECTORY)
    add_custom_command(
      OUTPUT "${VARIANT_OUTPUT_SPV}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${VARIANT_OUTPUT_DIR}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${VARIANT_DEPFILE_DIR}"
      COMMAND "${Python3_EXECUTABLE}"
              "${CMAKE_SOURCE_DIR}/scripts/shaders/compile_material_source_variants.py"
              --glslc "${GLSLC_EXECUTABLE}"
              --input "${SHADER_FILE}"
              --include-root "${GLSL_INPUT_DIR}"
              --contract "${LX_MATERIAL_SOURCE_VARIANT_CONTRACT}"
              --generated-source "${VARIANT_GENERATED_SOURCE}"
              --output "${VARIANT_OUTPUT_SPV}"
              --depfile "${VARIANT_DEPFILE}"
      DEPENDS "${SHADER_FILE}"
      DEPFILE "${VARIANT_DEPFILE}"
      COMMENT "Compiling material source shader variant: ${RELATIVE_SHADER_FILE} + ${LX_MATERIAL_SOURCE_VARIANT_CONTRACT}"
      VERBATIM
    )
    continue()
  endif()

  list(APPEND SPV_FILES "${OUTPUT_SPV}")
  set(OUTPUT_DEPFILE "${CMAKE_BINARY_DIR}/shader_deps/${RELATIVE_SHADER_FILE}.spv.d")
  get_filename_component(OUTPUT_DIR "${OUTPUT_SPV}" DIRECTORY)
  get_filename_component(DEPFILE_DIR "${OUTPUT_DEPFILE}" DIRECTORY)

  add_custom_command(
    OUTPUT "${OUTPUT_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUTPUT_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DEPFILE_DIR}"
    COMMAND "${GLSLC_EXECUTABLE}"
            -I "${GLSL_INPUT_DIR}"
            -MD
            -MF "${OUTPUT_DEPFILE}"
            -o "${OUTPUT_SPV}"
            "${SHADER_FILE}"
    DEPENDS "${SHADER_FILE}"
    DEPFILE "${OUTPUT_DEPFILE}"
    COMMENT "Compiling shader: ${FILE_NAME} -> ${OUTPUT_SPV}"
    VERBATIM
  )
endforeach()

add_custom_target(CompileShaders DEPENDS SyncShaderRuntimeSources ${SPV_FILES})
add_custom_target(CompileMaterialSourceShaderVariants
  DEPENDS SyncShaderRuntimeSources ${MATERIAL_SOURCE_VARIANT_SPV_FILES}
)
