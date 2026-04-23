if(NOT CMAKE_HOST_WIN32)
  return()
endif()

set(LX_WINDOWS_AUTO_IMPORT_MSVC_ENV ON CACHE BOOL
    "Automatically import the Visual Studio C++ toolchain environment on Windows")
set(LX_WINDOWS_VSWHERE_PATH "" CACHE FILEPATH
    "Optional path to vswhere.exe used to locate Visual Studio")
set(LX_WINDOWS_VS_INSTALLATION_PATH "" CACHE PATH
    "Optional Visual Studio installation root used for MSVC environment bootstrap")
set(LX_WINDOWS_REQUIRED_VS_MAJOR "2022" CACHE STRING
    "Required Visual Studio major version on Windows. This project supports Visual Studio 2022 only.")

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
  set(_lx_cmd_script "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/lx_msvc_env_bootstrap.cmd")
  get_filename_component(_lx_cmd_script_dir "${_lx_cmd_script}" DIRECTORY)
  file(MAKE_DIRECTORY "${_lx_cmd_script_dir}")
  file(WRITE "${_lx_cmd_script}"
    "@echo off\r\n"
    "call \"${bootstrap_script}\" ${bootstrap_args} >nul\r\n"
    "if errorlevel 1 exit /b %errorlevel%\r\n"
    "set\r\n")

  execute_process(
    COMMAND cmd /d /c "${_lx_cmd_script}"
    OUTPUT_VARIABLE _lx_env_dump
    ERROR_VARIABLE _lx_env_error
    RESULT_VARIABLE _lx_env_result
  )

  file(REMOVE "${_lx_cmd_script}")

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

function(_lx_normalize_path input_path output_var)
  if(NOT input_path)
    set(${output_var} "" PARENT_SCOPE)
    return()
  endif()

  set(_lx_normalized_path "${input_path}")
  string(REPLACE "\\" "/" _lx_normalized_path "${_lx_normalized_path}")
  string(REGEX REPLACE "/+$" "" _lx_normalized_path "${_lx_normalized_path}")
  set(${output_var} "${_lx_normalized_path}" PARENT_SCOPE)
endfunction()

function(_lx_map_visual_studio_version input output_var)
  if("${input}" MATCHES "^17(\\.|$)")
    set(${output_var} "2022" PARENT_SCOPE)
  elseif("${input}" MATCHES "^16(\\.|$)")
    set(${output_var} "2019" PARENT_SCOPE)
  elseif("${input}" MATCHES "^15(\\.|$)")
    set(${output_var} "2017" PARENT_SCOPE)
  else()
    set(${output_var} "${input}" PARENT_SCOPE)
  endif()
endfunction()

function(_lx_detect_vs_major_from_install_root install_root output_var)
  if("${install_root}" MATCHES [[/Microsoft Visual Studio/([0-9]+)/[^/]+/?$]])
    set(${output_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  else()
    set(${output_var} "" PARENT_SCOPE)
  endif()
endfunction()

function(_lx_is_supported_vs_major vs_major output_var)
  if("${vs_major}" STREQUAL "2022")
    set(${output_var} TRUE PARENT_SCOPE)
  else()
    set(${output_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(_lx_append_list_entry list_var entry)
  set(_lx_items "${${list_var}}")
  list(APPEND _lx_items "${entry}")
  list(REMOVE_DUPLICATES _lx_items)
  set(${list_var} "${_lx_items}" PARENT_SCOPE)
endfunction()

function(_lx_append_vs_candidate list_var install_root reason)
  if(NOT install_root)
    return()
  endif()

  _lx_normalize_path("${install_root}" _lx_install_root_normalized)
  get_filename_component(_lx_candidate_root "${_lx_install_root_normalized}" ABSOLUTE)
  if(NOT EXISTS "${_lx_candidate_root}")
    return()
  endif()

  _lx_append_list_entry(${list_var} "${_lx_candidate_root}|${reason}")
endfunction()

function(_lx_collect_vswhere_install_paths vswhere_path out_install_paths out_failures)
  set(_lx_install_paths "")
  set(_lx_failures "")

  if(NOT EXISTS "${vswhere_path}")
    set(${out_install_paths} "" PARENT_SCOPE)
    set(${out_failures} "vswhere not found: ${vswhere_path}" PARENT_SCOPE)
    return()
  endif()

  set(_lx_vswhere_queries
    "-all;-products;*;-requires;Microsoft.VisualStudio.Component.VC.Tools.x86.x64;-property;installationPath"
    "-all;-products;*;-requiresAny;-requires;Microsoft.VisualStudio.Component.VC.Tools.x86.x64;Microsoft.VisualStudio.Workload.VCTools;-property;installationPath"
    "-all;-products;*;-property;installationPath")

  foreach(_lx_query IN LISTS _lx_vswhere_queries)
    execute_process(
      COMMAND "${vswhere_path}" ${_lx_query}
      OUTPUT_VARIABLE _lx_vswhere_output
      ERROR_VARIABLE _lx_vswhere_error
      RESULT_VARIABLE _lx_vswhere_result
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT _lx_vswhere_result EQUAL 0)
      string(STRIP "${_lx_vswhere_error}" _lx_vswhere_error)
      if(_lx_vswhere_error)
        list(APPEND _lx_failures "${vswhere_path}: ${_lx_vswhere_error}")
      endif()
      continue()
    endif()

    if(NOT _lx_vswhere_output)
      continue()
    endif()

    string(REPLACE "\r\n" "\n" _lx_vswhere_output "${_lx_vswhere_output}")
    string(REPLACE "\r" "\n" _lx_vswhere_output "${_lx_vswhere_output}")
    string(REPLACE "\n" ";" _lx_vswhere_lines "${_lx_vswhere_output}")

    foreach(_lx_line IN LISTS _lx_vswhere_lines)
      string(STRIP "${_lx_line}" _lx_line)
      if(_lx_line AND EXISTS "${_lx_line}")
        list(APPEND _lx_install_paths "${_lx_line}")
      endif()
    endforeach()
  endforeach()

  list(REMOVE_DUPLICATES _lx_install_paths)
  list(REMOVE_DUPLICATES _lx_failures)
  set(${out_install_paths} "${_lx_install_paths}" PARENT_SCOPE)
  set(${out_failures} "${_lx_failures}" PARENT_SCOPE)
endfunction()

function(_lx_collect_vs_install_roots base_dir out_install_roots)
  set(_lx_install_roots "")

  _lx_normalize_path("${base_dir}" _lx_base_dir)
  if(NOT _lx_base_dir OR NOT EXISTS "${_lx_base_dir}/Microsoft Visual Studio")
    set(${out_install_roots} "" PARENT_SCOPE)
    return()
  endif()

  foreach(_lx_sku IN ITEMS Community Professional Enterprise BuildTools Preview)
    set(_lx_known_2022_root "${_lx_base_dir}/Microsoft Visual Studio/2022/${_lx_sku}")
    if(EXISTS "${_lx_known_2022_root}/Common7/Tools/VsDevCmd.bat"
       OR EXISTS "${_lx_known_2022_root}/VC/Auxiliary/Build/vcvarsall.bat")
      list(APPEND _lx_install_roots "${_lx_known_2022_root}")
    endif()
  endforeach()

  file(GLOB _lx_candidate_roots
    LIST_DIRECTORIES true
    "${_lx_base_dir}/Microsoft Visual Studio/*/*")
  list(SORT _lx_candidate_roots)
  list(REVERSE _lx_candidate_roots)

  foreach(_lx_candidate IN LISTS _lx_candidate_roots)
    if(NOT IS_DIRECTORY "${_lx_candidate}")
      continue()
    endif()

    if(EXISTS "${_lx_candidate}/Common7/Tools/VsDevCmd.bat"
       OR EXISTS "${_lx_candidate}/VC/Auxiliary/Build/vcvarsall.bat")
      list(APPEND _lx_install_roots "${_lx_candidate}")
    endif()
  endforeach()

  list(REMOVE_DUPLICATES _lx_install_roots)
  set(${out_install_roots} "${_lx_install_roots}" PARENT_SCOPE)
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
  set(_lx_existing_vs_major "")
  if(DEFINED ENV{VisualStudioVersion})
    _lx_map_visual_studio_version("$ENV{VisualStudioVersion}" _lx_existing_vs_major)
  elseif(DEFINED ENV{VSINSTALLDIR})
    _lx_detect_vs_major_from_install_root("$ENV{VSINSTALLDIR}" _lx_existing_vs_major)
  endif()

  if(LX_WINDOWS_REQUIRED_VS_MAJOR
     AND _lx_existing_vs_major
     AND NOT _lx_existing_vs_major STREQUAL LX_WINDOWS_REQUIRED_VS_MAJOR)
    message(FATAL_ERROR
      "Detected an existing Visual Studio developer environment for Visual Studio "
      "${_lx_existing_vs_major}, but this project requires Visual Studio "
      "${LX_WINDOWS_REQUIRED_VS_MAJOR}.")
  endif()

  if(_lx_existing_vs_major)
    _lx_is_supported_vs_major("${_lx_existing_vs_major}" _lx_existing_vs_supported)
    if(NOT _lx_existing_vs_supported)
      message(FATAL_ERROR
        "Detected an existing Visual Studio developer environment for Visual Studio "
        "${_lx_existing_vs_major}, but this project supports Visual Studio 2022 only.")
    endif()
  endif()

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

set(_lx_vs_candidates "")
set(_lx_scan_notes "")

if(LX_WINDOWS_VS_INSTALLATION_PATH AND EXISTS "${LX_WINDOWS_VS_INSTALLATION_PATH}")
  _lx_append_vs_candidate(_lx_vs_candidates "${LX_WINDOWS_VS_INSTALLATION_PATH}"
    "LX_WINDOWS_VS_INSTALLATION_PATH")
endif()

if(DEFINED ENV{VSINSTALLDIR} AND EXISTS "$ENV{VSINSTALLDIR}")
  _lx_append_vs_candidate(_lx_vs_candidates "$ENV{VSINSTALLDIR}" "VSINSTALLDIR")
endif()

if(DEFINED ENV{VCINSTALLDIR})
  get_filename_component(_lx_vs_from_vc "$ENV{VCINSTALLDIR}/../.." ABSOLUTE)
  if(EXISTS "${_lx_vs_from_vc}")
    _lx_append_vs_candidate(_lx_vs_candidates "${_lx_vs_from_vc}" "VCINSTALLDIR")
  endif()
endif()

set(_lx_vswhere_candidates "")
if(LX_WINDOWS_VSWHERE_PATH)
  list(APPEND _lx_vswhere_candidates "${LX_WINDOWS_VSWHERE_PATH}")
endif()
_lx_read_cmd_env("ProgramFiles(x86)" _lx_program_files_x86)
_lx_read_cmd_env("ProgramW6432" _lx_program_w6432)
if(_lx_program_files_x86)
  list(APPEND _lx_vswhere_candidates
    "${_lx_program_files_x86}/Microsoft Visual Studio/Installer/vswhere.exe")
endif()
set(_lx_program_files "$ENV{ProgramFiles}")
if(_lx_program_files)
  list(APPEND _lx_vswhere_candidates
    "${_lx_program_files}/Microsoft Visual Studio/Installer/vswhere.exe")
endif()
if(_lx_program_w6432)
  list(APPEND _lx_vswhere_candidates
    "${_lx_program_w6432}/Microsoft Visual Studio/Installer/vswhere.exe")
endif()
list(REMOVE_DUPLICATES _lx_vswhere_candidates)

foreach(_lx_vswhere IN LISTS _lx_vswhere_candidates)
  _lx_collect_vswhere_install_paths("${_lx_vswhere}" _lx_found_installs _lx_vswhere_failures)
  foreach(_lx_found_install IN LISTS _lx_found_installs)
    _lx_append_vs_candidate(_lx_vs_candidates "${_lx_found_install}"
      "vswhere (${_lx_vswhere})")
  endforeach()
  foreach(_lx_vswhere_failure IN LISTS _lx_vswhere_failures)
    list(APPEND _lx_scan_notes "${_lx_vswhere_failure}")
  endforeach()
endforeach()

foreach(_lx_comntools_env
    VS170COMNTOOLS
    VS160COMNTOOLS
    VS150COMNTOOLS
    VS140COMNTOOLS)
  if(DEFINED ENV{${_lx_comntools_env}})
    get_filename_component(_lx_comntools_root "$ENV{${_lx_comntools_env}}/../.." ABSOLUTE)
    if(EXISTS "${_lx_comntools_root}")
      _lx_append_vs_candidate(_lx_vs_candidates "${_lx_comntools_root}" "${_lx_comntools_env}")
    endif()
  endif()
endforeach()

set(_lx_default_scan_roots "")
if(_lx_program_files_x86)
  list(APPEND _lx_default_scan_roots "${_lx_program_files_x86}")
endif()
if(_lx_program_files)
  list(APPEND _lx_default_scan_roots "${_lx_program_files}")
endif()
if(_lx_program_w6432)
  list(APPEND _lx_default_scan_roots "${_lx_program_w6432}")
endif()
list(REMOVE_DUPLICATES _lx_default_scan_roots)
list(APPEND _lx_scan_notes "ProgramFiles=${_lx_program_files}")
list(APPEND _lx_scan_notes "ProgramFiles(x86)=${_lx_program_files_x86}")
list(APPEND _lx_scan_notes "ProgramW6432=${_lx_program_w6432}")

foreach(_lx_scan_root IN LISTS _lx_default_scan_roots)
  _lx_collect_vs_install_roots("${_lx_scan_root}" _lx_scanned_candidates)
  if(NOT _lx_scanned_candidates)
    list(APPEND _lx_scan_notes
      "No Visual Studio installations found under ${_lx_scan_root}/Microsoft Visual Studio")
  endif()
  foreach(_lx_candidate IN LISTS _lx_scanned_candidates)
    _lx_append_vs_candidate(_lx_vs_candidates "${_lx_candidate}"
      "filesystem scan (${_lx_scan_root})")
    list(APPEND _lx_scan_notes "Found Visual Studio candidate: ${_lx_candidate}")
  endforeach()
endforeach()

set(_lx_bootstrap_attempts "")
set(_lx_bootstrap_reason "")

foreach(_lx_vs_candidate IN LISTS _lx_vs_candidates)
  string(REPLACE "|" ";" _lx_vs_candidate_fields "${_lx_vs_candidate}")
  list(GET _lx_vs_candidate_fields 0 _lx_vs_install_path)
  list(GET _lx_vs_candidate_fields 1 _lx_vs_source)

  _lx_detect_vs_major_from_install_root("${_lx_vs_install_path}" _lx_vs_major)
  if(_lx_vs_major)
    _lx_is_supported_vs_major("${_lx_vs_major}" _lx_vs_supported)
    if(NOT _lx_vs_supported)
      list(APPEND _lx_scan_notes
        "Skipping ${_lx_vs_install_path}: Visual Studio ${_lx_vs_major} is installed but unsupported; this project requires Visual Studio 2022")
      continue()
    endif()
  endif()
  if(LX_WINDOWS_REQUIRED_VS_MAJOR
     AND _lx_vs_major
     AND NOT _lx_vs_major STREQUAL LX_WINDOWS_REQUIRED_VS_MAJOR)
    list(APPEND _lx_scan_notes
      "Skipping ${_lx_vs_install_path}: Visual Studio ${_lx_vs_major} does not match required ${LX_WINDOWS_REQUIRED_VS_MAJOR}")
    continue()
  endif()

  set(_lx_vsdevcmd_bat "${_lx_vs_install_path}/Common7/Tools/VsDevCmd.bat")
  set(_lx_vcvarsall_bat "${_lx_vs_install_path}/VC/Auxiliary/Build/vcvarsall.bat")
  set(_lx_vcvars64_bat "${_lx_vs_install_path}/VC/Auxiliary/Build/vcvars64.bat")
  set(_lx_common7_tools_dir "${_lx_vs_install_path}/Common7/Tools")
  set(_lx_vc_aux_build_dir "${_lx_vs_install_path}/VC/Auxiliary/Build")
  set(_lx_candidate_has_bootstrap FALSE)

  if(IS_DIRECTORY "${_lx_common7_tools_dir}")
    set(_lx_common7_tools_state "present")
  else()
    set(_lx_common7_tools_state "missing")
  endif()
  if(IS_DIRECTORY "${_lx_vc_aux_build_dir}")
    set(_lx_vc_aux_build_state "present")
  else()
    set(_lx_vc_aux_build_state "missing")
  endif()
  if(EXISTS "${_lx_vsdevcmd_bat}")
    set(_lx_vsdevcmd_state "present")
  else()
    set(_lx_vsdevcmd_state "missing")
  endif()
  if(EXISTS "${_lx_vcvarsall_bat}")
    set(_lx_vcvarsall_state "present")
  else()
    set(_lx_vcvarsall_state "missing")
  endif()
  if(EXISTS "${_lx_vcvars64_bat}")
    set(_lx_vcvars64_state "present")
  else()
    set(_lx_vcvars64_state "missing")
  endif()

  list(APPEND _lx_scan_notes
    "Inspecting ${_lx_vs_install_path} from ${_lx_vs_source}: Common7/Tools=${_lx_common7_tools_state}, VC/Auxiliary/Build=${_lx_vc_aux_build_state}, VsDevCmd=${_lx_vsdevcmd_state}, vcvarsall=${_lx_vcvarsall_state}, vcvars64=${_lx_vcvars64_state}")

  if(EXISTS "${_lx_vsdevcmd_bat}")
    set(_lx_candidate_has_bootstrap TRUE)
    list(APPEND _lx_bootstrap_attempts
      "${_lx_vsdevcmd_bat}|-no_logo -arch=${_lx_target_arch} -host_arch=${_lx_host_arch}|${_lx_vs_source} -> VsDevCmd")
  endif()
  if(EXISTS "${_lx_vcvarsall_bat}")
    set(_lx_candidate_has_bootstrap TRUE)
    list(APPEND _lx_bootstrap_attempts
      "${_lx_vcvarsall_bat}|${_lx_target_arch}|${_lx_vs_source} -> vcvarsall(${_lx_target_arch})")
    if(NOT _lx_target_arch STREQUAL _lx_host_arch)
      list(APPEND _lx_bootstrap_attempts
        "${_lx_vcvarsall_bat}|${_lx_host_arch}_${_lx_target_arch}|${_lx_vs_source} -> vcvarsall(${_lx_host_arch}_${_lx_target_arch})")
    endif()
  endif()
  if(_lx_target_arch STREQUAL "amd64" AND EXISTS "${_lx_vcvars64_bat}")
    set(_lx_candidate_has_bootstrap TRUE)
    list(APPEND _lx_bootstrap_attempts
      "${_lx_vcvars64_bat}||${_lx_vs_source} -> vcvars64")
  endif()
  if(NOT _lx_candidate_has_bootstrap)
    list(APPEND _lx_scan_notes
      "Candidate ${_lx_vs_install_path} was found, but no bootstrap scripts were present under Common7/Tools or VC/Auxiliary/Build")
  endif()
endforeach()

list(REMOVE_DUPLICATES _lx_bootstrap_attempts)
list(REMOVE_DUPLICATES _lx_scan_notes)

set(_lx_cl_from_path "")
find_program(_lx_cl_from_path cl)
if(_lx_cl_from_path)
  list(APPEND _lx_bootstrap_attempts
    "__PATH_CL__||cl.exe already in PATH")
endif()

if(NOT _lx_bootstrap_attempts)
  list(APPEND _lx_scan_notes
    "No Visual Studio bootstrap attempts were generated from discovered candidates")
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

string(JOIN "\n  - " _lx_scan_summary ${_lx_scan_notes})
if(_lx_scan_summary)
  set(_lx_scan_summary "Discovery notes:\n  - ${_lx_scan_summary}\n")
endif()

message(FATAL_ERROR
  "Unable to locate or import a usable Visual Studio C++ build environment.\n"
  "Required Visual Studio major version: ${LX_WINDOWS_REQUIRED_VS_MAJOR}\n"
  "${_lx_scan_summary}"
  "${_lx_failure_summary}"
  "Install the Desktop development with C++ workload, launch from a VS developer "
  "shell, or set LX_WINDOWS_VS_INSTALLATION_PATH / LX_WINDOWS_VSWHERE_PATH.")
