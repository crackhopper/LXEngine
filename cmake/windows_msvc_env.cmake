if(NOT CMAKE_HOST_WIN32)
  return()
endif()

set(LX_WINDOWS_AUTO_IMPORT_MSVC_ENV ON CACHE BOOL
    "Automatically import the Visual Studio C++ toolchain environment on Windows")
set(LX_WINDOWS_VS_INSTALLATION_PATH "" CACHE PATH
    "Optional Visual Studio installation root used for MSVC environment bootstrap")
set(LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV ON CACHE BOOL
    "Enable detailed Windows MSVC bootstrap logging")

include("${CMAKE_CURRENT_LIST_DIR}/windows_msvc_env_impl.cmake")

if(NOT LX_WINDOWS_AUTO_IMPORT_MSVC_ENV)
  lx_windows_msvc_log_result("Windows MSVC bootstrap disabled")
  return()
endif()

lx_windows_msvc_log_phase("entry")
lx_windows_msvc_log_result("generator=${CMAKE_GENERATOR}")
lx_windows_msvc_log_result("source_dir=${CMAKE_SOURCE_DIR}")
lx_windows_msvc_log_result("build_dir=${CMAKE_BINARY_DIR}")
lx_windows_msvc_log_result("debug_logging=${LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV}")

if(CMAKE_GENERATOR MATCHES "^Visual Studio")
  lx_windows_msvc_log_result("Visual Studio generator detected; bootstrap not needed")
  return()
endif()

if(DEFINED CMAKE_C_COMPILER OR DEFINED CMAKE_CXX_COMPILER)
  lx_windows_msvc_log_result("Compiler already specified; bootstrap not needed")
  return()
endif()

lx_windows_msvc_check_existing_env(_lx_existing_env_ready _lx_existing_reason)
if(_lx_existing_env_ready)
  lx_windows_msvc_log_result("Existing MSVC environment detected: ${_lx_existing_reason}")
  return()
endif()

lx_windows_msvc_find_install(_lx_vs_root _lx_bootstrap_script)
lx_windows_msvc_import_env("${_lx_vs_root}" "${_lx_bootstrap_script}")
