if(NOT CMAKE_HOST_WIN32)
  return()
endif()

set(LX_WINDOWS_AUTO_IMPORT_MSVC_ENV ON CACHE BOOL
    "Automatically import the Visual Studio C++ toolchain environment on Windows")
set(LX_WINDOWS_VSWHERE_PATH "" CACHE FILEPATH
    "Optional path to vswhere.exe used to locate Visual Studio")
set(LX_WINDOWS_VS_INSTALLATION_PATH "" CACHE PATH
    "Optional Visual Studio installation root used for MSVC environment bootstrap")

if(NOT LX_WINDOWS_AUTO_IMPORT_MSVC_ENV)
  message(STATUS "Windows MSVC environment auto import disabled")
  return()
endif()

if(CMAKE_GENERATOR MATCHES "^Visual Studio")
  return()
endif()

if(DEFINED CMAKE_C_COMPILER OR DEFINED CMAKE_CXX_COMPILER)
  return()
endif()

function(_lx_map_vs_arch input output_var)
  string(TOLOWER "${input}" _lx_arch)
  if(_lx_arch STREQUAL "win32" OR _lx_arch STREQUAL "x86")
    set(${output_var} "x86" PARENT_SCOPE)
  elseif(_lx_arch STREQUAL "x64" OR _lx_arch STREQUAL "amd64")
    set(${output_var} "amd64" PARENT_SCOPE)
  elseif(_lx_arch STREQUAL "arm64")
    set(${output_var} "arm64" PARENT_SCOPE)
  else()
    set(${output_var} "amd64" PARENT_SCOPE)
  endif()
endfunction()

function(_lx_try_query_vswhere vswhere_path out_install_path out_reason)
  if(NOT EXISTS "${vswhere_path}")
    set(${out_install_path} "" PARENT_SCOPE)
    set(${out_reason} "" PARENT_SCOPE)
    return()
  endif()

  set(_lx_vswhere_queries
    "-latest;-products;*;-requires;Microsoft.VisualStudio.Component.VC.Tools.x86.x64;-property;installationPath"
    "-latest;-products;*;-requiresAny;-requires;Microsoft.VisualStudio.Component.VC.Tools.x86.x64;Microsoft.VisualStudio.Workload.VCTools;-property;installationPath"
    "-latest;-products;*;-property;installationPath")

  foreach(_lx_query IN LISTS _lx_vswhere_queries)
    execute_process(
      COMMAND "${vswhere_path}" ${_lx_query}
      OUTPUT_VARIABLE _lx_vswhere_output
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(_lx_vswhere_output AND EXISTS "${_lx_vswhere_output}")
      set(${out_install_path} "${_lx_vswhere_output}" PARENT_SCOPE)
      set(${out_reason} "vswhere (${vswhere_path})" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${out_install_path} "" PARENT_SCOPE)
  set(${out_reason} "" PARENT_SCOPE)
endfunction()

function(_lx_try_import_env_dump bootstrap_script bootstrap_args out_result out_error)
  set(_lx_cmd "call \"${bootstrap_script}\" ${bootstrap_args} >nul && set")
  execute_process(
    COMMAND cmd /d /c "${_lx_cmd}"
    OUTPUT_VARIABLE _lx_env_dump
    ERROR_VARIABLE _lx_env_error
    RESULT_VARIABLE _lx_env_result
  )

  if(NOT _lx_env_result EQUAL 0)
    string(STRIP "${_lx_env_error}" _lx_env_error)
    if(NOT _lx_env_error)
      set(_lx_env_error "bootstrap command returned ${_lx_env_result}")
    endif()
    set(${out_result} "" PARENT_SCOPE)
    set(${out_error} "${_lx_env_error}" PARENT_SCOPE)
    return()
  endif()

  set(${out_result} "${_lx_env_dump}" PARENT_SCOPE)
  set(${out_error} "" PARENT_SCOPE)
endfunction()

function(_lx_read_cmd_env env_name out_value)
  execute_process(
    COMMAND cmd /d /c "echo %${env_name}%"
    OUTPUT_VARIABLE _lx_env_value
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _lx_env_result
    ERROR_QUIET
  )

  if(_lx_env_result EQUAL 0 AND NOT _lx_env_value STREQUAL "%${env_name}%")
    set(${out_value} "${_lx_env_value}" PARENT_SCOPE)
  else()
    set(${out_value} "" PARENT_SCOPE)
  endif()
endfunction()

set(_lx_existing_env_ready FALSE)
if(DEFINED ENV{VSCMD_VER} AND DEFINED ENV{VCToolsInstallDir})
  if(EXISTS "$ENV{VCToolsInstallDir}")
    set(_lx_existing_env_ready TRUE)
  endif()
endif()
if(NOT _lx_existing_env_ready AND DEFINED ENV{VCINSTALLDIR})
  if(EXISTS "$ENV{VCINSTALLDIR}")
    set(_lx_existing_env_ready TRUE)
  endif()
endif()
if(_lx_existing_env_ready)
  message(STATUS
    "Using existing Visual Studio developer environment: "
    "$ENV{VCToolsInstallDir}$ENV{VCINSTALLDIR}")
  return()
endif()

if(CMAKE_GENERATOR_PLATFORM)
  set(_lx_requested_arch "${CMAKE_GENERATOR_PLATFORM}")
elseif(DEFINED ENV{Platform})
  set(_lx_requested_arch "$ENV{Platform}")
elseif(CMAKE_HOST_SYSTEM_PROCESSOR)
  set(_lx_requested_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
else()
  set(_lx_requested_arch "x64")
endif()

_lx_map_vs_arch("${_lx_requested_arch}" _lx_target_arch)
_lx_map_vs_arch("${CMAKE_HOST_SYSTEM_PROCESSOR}" _lx_host_arch)

set(_lx_vs_install_path "")
set(_lx_vs_source "")

if(LX_WINDOWS_VS_INSTALLATION_PATH AND EXISTS "${LX_WINDOWS_VS_INSTALLATION_PATH}")
  set(_lx_vs_install_path "${LX_WINDOWS_VS_INSTALLATION_PATH}")
  set(_lx_vs_source "LX_WINDOWS_VS_INSTALLATION_PATH")
endif()

if(NOT _lx_vs_install_path AND DEFINED ENV{VSINSTALLDIR})
  if(EXISTS "$ENV{VSINSTALLDIR}")
    set(_lx_vs_install_path "$ENV{VSINSTALLDIR}")
    set(_lx_vs_source "VSINSTALLDIR")
  endif()
endif()

if(NOT _lx_vs_install_path AND DEFINED ENV{VCINSTALLDIR})
  get_filename_component(_lx_vs_from_vc "$ENV{VCINSTALLDIR}/../.." ABSOLUTE)
  if(EXISTS "${_lx_vs_from_vc}")
    set(_lx_vs_install_path "${_lx_vs_from_vc}")
    set(_lx_vs_source "VCINSTALLDIR")
  endif()
endif()

set(_lx_vswhere_candidates "")
if(LX_WINDOWS_VSWHERE_PATH)
  list(APPEND _lx_vswhere_candidates "${LX_WINDOWS_VSWHERE_PATH}")
endif()
_lx_read_cmd_env("ProgramFiles(x86)" _lx_program_files_x86)
if(_lx_program_files_x86)
  list(APPEND _lx_vswhere_candidates
    "${_lx_program_files_x86}/Microsoft Visual Studio/Installer/vswhere.exe")
endif()
set(_lx_program_files "$ENV{ProgramFiles}")
if(_lx_program_files)
  list(APPEND _lx_vswhere_candidates
    "${_lx_program_files}/Microsoft Visual Studio/Installer/vswhere.exe")
endif()
list(REMOVE_DUPLICATES _lx_vswhere_candidates)

if(NOT _lx_vs_install_path)
  foreach(_lx_vswhere IN LISTS _lx_vswhere_candidates)
    _lx_try_query_vswhere("${_lx_vswhere}" _lx_found_install _lx_found_reason)
    if(_lx_found_install)
      set(_lx_vs_install_path "${_lx_found_install}")
      set(_lx_vs_source "${_lx_found_reason}")
      break()
    endif()
  endforeach()
endif()

if(NOT _lx_vs_install_path)
  foreach(_lx_comntools_env
      VS170COMNTOOLS
      VS160COMNTOOLS
      VS150COMNTOOLS
      VS140COMNTOOLS)
    if(DEFINED ENV{${_lx_comntools_env}})
      get_filename_component(_lx_comntools_root "$ENV{${_lx_comntools_env}}/../.." ABSOLUTE)
      if(EXISTS "${_lx_comntools_root}")
        set(_lx_vs_install_path "${_lx_comntools_root}")
        set(_lx_vs_source "${_lx_comntools_env}")
        break()
      endif()
    endif()
  endforeach()
endif()

if(NOT _lx_vs_install_path)
  set(_lx_default_vs_roots "")
  if(_lx_program_files_x86)
    list(APPEND _lx_default_vs_roots
      "${_lx_program_files_x86}/Microsoft Visual Studio/2022/BuildTools"
      "${_lx_program_files_x86}/Microsoft Visual Studio/2022/Community"
      "${_lx_program_files_x86}/Microsoft Visual Studio/2022/Professional"
      "${_lx_program_files_x86}/Microsoft Visual Studio/2022/Enterprise"
      "${_lx_program_files_x86}/Microsoft Visual Studio/2019/BuildTools"
      "${_lx_program_files_x86}/Microsoft Visual Studio/2019/Community"
      "${_lx_program_files_x86}/Microsoft Visual Studio/2019/Professional"
      "${_lx_program_files_x86}/Microsoft Visual Studio/2019/Enterprise")
  endif()
  foreach(_lx_candidate IN LISTS _lx_default_vs_roots)
    if(EXISTS "${_lx_candidate}/VC/Auxiliary/Build/vcvarsall.bat"
       OR EXISTS "${_lx_candidate}/Common7/Tools/VsDevCmd.bat")
      set(_lx_vs_install_path "${_lx_candidate}")
      set(_lx_vs_source "default install path scan")
      break()
    endif()
  endforeach()
endif()

set(_lx_bootstrap_attempts "")
set(_lx_bootstrap_reason "")

if(_lx_vs_install_path)
  set(_lx_vsdevcmd_bat "${_lx_vs_install_path}/Common7/Tools/VsDevCmd.bat")
  set(_lx_vcvarsall_bat "${_lx_vs_install_path}/VC/Auxiliary/Build/vcvarsall.bat")
  set(_lx_vcvars64_bat "${_lx_vs_install_path}/VC/Auxiliary/Build/vcvars64.bat")

  if(EXISTS "${_lx_vsdevcmd_bat}")
    list(APPEND _lx_bootstrap_attempts
      "${_lx_vsdevcmd_bat}|-no_logo -arch=${_lx_target_arch} -host_arch=${_lx_host_arch}|${_lx_vs_source} -> VsDevCmd")
  endif()
  if(EXISTS "${_lx_vcvarsall_bat}")
    list(APPEND _lx_bootstrap_attempts
      "${_lx_vcvarsall_bat}|${_lx_target_arch}|${_lx_vs_source} -> vcvarsall(${_lx_target_arch})")
    if(NOT _lx_target_arch STREQUAL _lx_host_arch)
      list(APPEND _lx_bootstrap_attempts
        "${_lx_vcvarsall_bat}|${_lx_host_arch}_${_lx_target_arch}|${_lx_vs_source} -> vcvarsall(${_lx_host_arch}_${_lx_target_arch})")
    endif()
  endif()
  if(_lx_target_arch STREQUAL "amd64" AND EXISTS "${_lx_vcvars64_bat}")
    list(APPEND _lx_bootstrap_attempts
      "${_lx_vcvars64_bat}||${_lx_vs_source} -> vcvars64")
  endif()
endif()

set(_lx_cl_from_path "")
find_program(_lx_cl_from_path cl)
if(_lx_cl_from_path)
  list(APPEND _lx_bootstrap_attempts
    "__PATH_CL__||cl.exe already in PATH")
endif()

set(_lx_env_dump "")
set(_lx_import_source "")
set(_lx_failures "")

foreach(_lx_attempt IN LISTS _lx_bootstrap_attempts)
  string(REPLACE "|" ";" _lx_attempt_fields "${_lx_attempt}")
  list(GET _lx_attempt_fields 0 _lx_attempt_script)
  list(GET _lx_attempt_fields 1 _lx_attempt_args)
  list(GET _lx_attempt_fields 2 _lx_attempt_reason)

  if(_lx_attempt_script STREQUAL "__PATH_CL__")
    if(DEFINED ENV{PATH} AND DEFINED ENV{INCLUDE} AND DEFINED ENV{LIB})
      set(_lx_import_source "${_lx_attempt_reason}")
      break()
    endif()
    list(APPEND _lx_failures
      "${_lx_attempt_reason}: PATH has cl.exe but INCLUDE/LIB are incomplete")
    continue()
  endif()

  _lx_try_import_env_dump("${_lx_attempt_script}" "${_lx_attempt_args}"
                          _lx_attempt_dump _lx_attempt_error)
  if(_lx_attempt_dump)
    set(_lx_env_dump "${_lx_attempt_dump}")
    set(_lx_import_source "${_lx_attempt_reason}")
    break()
  endif()

  if(_lx_attempt_error)
    list(APPEND _lx_failures "${_lx_attempt_reason}: ${_lx_attempt_error}")
  else()
    list(APPEND _lx_failures "${_lx_attempt_reason}: bootstrap failed")
  endif()
endforeach()

if(_lx_env_dump)
  string(REPLACE "\r\n" "\n" _lx_env_dump "${_lx_env_dump}")
  string(REPLACE "\r" "\n" _lx_env_dump "${_lx_env_dump}")
  string(REPLACE "\n" ";" _lx_env_lines "${_lx_env_dump}")

  set(_lx_imported_keys "")
  foreach(_lx_line IN LISTS _lx_env_lines)
    if(_lx_line MATCHES "^([^=]+)=(.*)$")
      set(_lx_name "${CMAKE_MATCH_1}")
      set(_lx_value "${CMAKE_MATCH_2}")
      if(NOT _lx_name MATCHES "^=")
        set(ENV{${_lx_name}} "${_lx_value}")
        if(_lx_name MATCHES "^(PATH|INCLUDE|LIB|LIBPATH|VCToolsInstallDir|VCINSTALLDIR|VSINSTALLDIR|WindowsSdkDir|WindowsSdkVersion|UCRTVersion|UniversalCRTSdkDir)$")
          list(APPEND _lx_imported_keys "${_lx_name}")
        endif()
      endif()
    endif()
  endforeach()

  list(REMOVE_DUPLICATES _lx_imported_keys)
  string(JOIN ", " _lx_imported_keys _lx_imported_summary)
  message(STATUS
    "Imported Visual Studio developer environment for ${_lx_target_arch} via "
    "${_lx_import_source}")
  if(_lx_imported_summary)
    message(STATUS "Imported environment keys: ${_lx_imported_summary}")
  endif()
  return()
endif()

if(_lx_import_source)
  message(STATUS
    "Using existing compiler environment via ${_lx_import_source}")
  return()
endif()

string(JOIN "\n  - " _lx_failure_summary ${_lx_failures})
if(_lx_failure_summary)
  set(_lx_failure_summary "Tried:\n  - ${_lx_failure_summary}\n")
endif()

message(FATAL_ERROR
  "Unable to locate or import a usable Visual Studio C++ build environment.\n"
  "${_lx_failure_summary}"
  "Install the Desktop development with C++ workload, launch from a VS developer "
  "shell, or set LX_WINDOWS_VS_INSTALLATION_PATH / LX_WINDOWS_VSWHERE_PATH.")
