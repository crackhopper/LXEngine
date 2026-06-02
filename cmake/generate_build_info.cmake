function(lx_escape_cpp_string OUT_VAR INPUT_VALUE)
  set(_value "${INPUT_VALUE}")
  string(REPLACE "\\" "\\\\" _value "${_value}")
  string(REPLACE "\"" "\\\"" _value "${_value}")
  string(REPLACE "\n" "\\n" _value "${_value}")
  string(REPLACE "\r" "\\r" _value "${_value}")
  string(REPLACE "\t" "\\t" _value "${_value}")
  set(${OUT_VAR} "${_value}" PARENT_SCOPE)
endfunction()

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED OUTPUT_FILE)
  message(FATAL_ERROR "OUTPUT_FILE is required")
endif()
if(NOT DEFINED PROJECT_VERSION)
  set(PROJECT_VERSION "unknown")
endif()
if(NOT DEFINED BUILD_TYPE OR "${BUILD_TYPE}" STREQUAL "")
  set(BUILD_TYPE "Default")
endif()
if(NOT DEFINED PLATFORM)
  set(PLATFORM "unknown")
endif()

execute_process(
  COMMAND git -C "${SOURCE_DIR}" rev-parse --short=12 HEAD
  OUTPUT_VARIABLE LXE_BUILD_GIT_COMMIT_SHORT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
execute_process(
  COMMAND git -C "${SOURCE_DIR}" status --porcelain
  OUTPUT_VARIABLE LXE_BUILD_GIT_STATUS
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if("${LXE_BUILD_GIT_COMMIT_SHORT}" STREQUAL "")
  set(LXE_BUILD_GIT_COMMIT_SHORT "unknown")
endif()
if("${LXE_BUILD_GIT_STATUS}" STREQUAL "")
  set(LXE_BUILD_GIT_DIRTY 0)
else()
  set(LXE_BUILD_GIT_DIRTY 1)
endif()

lx_escape_cpp_string(LXE_BUILD_PROJECT_VERSION_CPP "${PROJECT_VERSION}")
lx_escape_cpp_string(LXE_BUILD_GIT_COMMIT_SHORT_CPP "${LXE_BUILD_GIT_COMMIT_SHORT}")
lx_escape_cpp_string(LXE_BUILD_TYPE_CPP "${BUILD_TYPE}")
lx_escape_cpp_string(LXE_BUILD_PLATFORM_CPP "${PLATFORM}")

set(_content "#pragma once

#define LXE_BUILD_PROJECT_VERSION \"${LXE_BUILD_PROJECT_VERSION_CPP}\"
#define LXE_BUILD_GIT_COMMIT_SHORT \"${LXE_BUILD_GIT_COMMIT_SHORT_CPP}\"
#define LXE_BUILD_GIT_DIRTY ${LXE_BUILD_GIT_DIRTY}
#define LXE_BUILD_TYPE \"${LXE_BUILD_TYPE_CPP}\"
#define LXE_BUILD_PLATFORM \"${LXE_BUILD_PLATFORM_CPP}\"
")

if(EXISTS "${OUTPUT_FILE}")
  file(READ "${OUTPUT_FILE}" _old_content)
else()
  set(_old_content "")
endif()

if(NOT "${_old_content}" STREQUAL "${_content}")
  get_filename_component(_output_dir "${OUTPUT_FILE}" DIRECTORY)
  file(MAKE_DIRECTORY "${_output_dir}")
  file(WRITE "${OUTPUT_FILE}" "${_content}")
endif()
