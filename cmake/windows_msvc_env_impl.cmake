function(lx_windows_msvc_log_detail message_text)
  if(LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV)
    message(STATUS "[windows_msvc_env] ${message_text}")
  endif()
endfunction()

function(lx_windows_msvc_log_result result_text)
  message(STATUS "[windows_msvc_env] ${result_text}")
endfunction()

function(lx_windows_msvc_probe_program program_label)
  if(NOT LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV)
    return()
  endif()

  find_program(_lx_probe_result NAMES "${program_label}.exe" "${program_label}")
  if(_lx_probe_result)
    lx_windows_msvc_log_result("${program_label}=${_lx_probe_result}")
  else()
    lx_windows_msvc_log_result("${program_label}=NOT FOUND")
  endif()
  unset(_lx_probe_result CACHE)
endfunction()

function(lx_windows_msvc_check_existing_env out_ready)
  find_program(_lx_existing_cl NAMES cl.exe cl)
  if(_lx_existing_cl AND DEFINED ENV{VCTOOLSINSTALLDIR} AND EXISTS "$ENV{VCTOOLSINSTALLDIR}")
    set(${out_ready} TRUE PARENT_SCOPE)
  elseif(_lx_existing_cl AND DEFINED ENV{VCINSTALLDIR} AND EXISTS "$ENV{VCINSTALLDIR}")
    set(${out_ready} TRUE PARENT_SCOPE)
  else()
    set(${out_ready} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(lx_windows_msvc_import_env)
  set(_lx_gen_cmake "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/lx_windows_msvc_env.gen.cmake")
  set(_lx_log_file "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/lx_windows_msvc_env.log")
  set(_lx_debug_enabled 0)
  if(LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV)
    set(_lx_debug_enabled 1)
  endif()

  find_program(_lx_python_executable NAMES python)
  if(NOT _lx_python_executable)
    find_program(_lx_python_executable NAMES python3)
  endif()
  if(NOT _lx_python_executable)
    file(REMOVE "${_lx_cmd_script}")
    message(FATAL_ERROR
      "[windows_msvc_env] Python was not found, but it is required to import the "
      "Visual Studio environment snapshot.")
  endif()

  execute_process(
    COMMAND
      "${_lx_python_executable}"
      "${CMAKE_CURRENT_LIST_DIR}/windows_msvc_env_dump.py"
      --output-cmake "${_lx_gen_cmake}"
      --output-log "${_lx_log_file}"
      --explicit-vs-root "${LX_WINDOWS_VS_INSTALLATION_PATH}"
      --debug-enabled "${_lx_debug_enabled}"
    OUTPUT_VARIABLE _lx_python_output
    ERROR_VARIABLE _lx_python_error
    RESULT_VARIABLE _lx_python_result
  )

  if(NOT _lx_python_result EQUAL 0 OR NOT EXISTS "${_lx_gen_cmake}")
    if(_lx_python_error)
      set(_lx_import_error "${_lx_python_error}")
    elseif(_lx_python_output)
      set(_lx_import_error "${_lx_python_output}")
    else()
      set(_lx_import_error "bootstrap generator failed with code ${_lx_python_result}")
    endif()
    message(FATAL_ERROR
      "[windows_msvc_env] Failed to generate imported Visual Studio environment: "
      "${_lx_import_error}")
  endif()

  include("${_lx_gen_cmake}")

  lx_windows_msvc_inject_toolchain_paths()

  lx_windows_msvc_log_result("Using Visual Studio root=$ENV{LX_WINDOWS_MSVC_VS_ROOT}")
  lx_windows_msvc_log_result("Using VsDevCmd=$ENV{LX_WINDOWS_MSVC_VSDEVCMD}")

  if(LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV)
    lx_windows_msvc_probe_program("cl")
    lx_windows_msvc_probe_program("link")
    lx_windows_msvc_probe_program("rc")
    lx_windows_msvc_probe_program("mt")
    lx_windows_msvc_probe_program("ninja")
  endif()
endfunction()

# .gen.cmake's `set(ENV{...})` only lives inside the configure process. ninja
# later spawns cl.exe / link.exe in fresh processes with empty env. Rather
# than re-applying env via a launcher at build time, bake the toolchain
# system paths directly into every compile / link command line:
#   INCLUDE -> CMAKE_<LANG>_STANDARD_INCLUDE_DIRECTORIES  (emits /I flags)
#   LIB     -> link_directories()                          (emits /LIBPATH:)
# This makes build.ninja self-contained and env-independent.
function(lx_windows_msvc_inject_toolchain_paths)
  # Windows env vars are ";"-separated, which is identical to CMake list syntax.
  set(_lx_inc "$ENV{INCLUDE}")
  set(_lx_lib "$ENV{LIB}")

  foreach(_lx_lang IN ITEMS C CXX)
    set(CMAKE_${_lx_lang}_STANDARD_INCLUDE_DIRECTORIES "${_lx_inc}"
        CACHE STRING "VS toolchain system include dirs, inlined as /I flags" FORCE)
  endforeach()

  if(NOT "${_lx_lib}" STREQUAL "")
    link_directories(${_lx_lib})
  endif()

  list(LENGTH _lx_inc _lx_inc_count)
  list(LENGTH _lx_lib _lx_lib_count)
  lx_windows_msvc_log_result("Inlined ${_lx_inc_count} INCLUDE path(s) via CMAKE_<LANG>_STANDARD_INCLUDE_DIRECTORIES")
  lx_windows_msvc_log_result("Injected ${_lx_lib_count} LIB path(s) via link_directories()")
endfunction()
