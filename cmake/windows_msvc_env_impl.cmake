function(lx_windows_msvc_log_detail message_text)
  if(LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV)
    message(STATUS "[windows_msvc_env] ${message_text}")
  endif()
endfunction()

function(lx_windows_msvc_log_phase phase_name)
  lx_windows_msvc_log_detail("phase: ${phase_name}")
endfunction()

function(lx_windows_msvc_log_action action_text)
  lx_windows_msvc_log_detail("next: ${action_text}")
endfunction()

function(lx_windows_msvc_log_result result_text)
  message(STATUS "[windows_msvc_env] ${result_text}")
endfunction()

function(lx_windows_msvc_probe_program program_label)
  lx_windows_msvc_log_action("probe ${program_label} in current PATH")
  find_program(_lx_program_path NAMES "${program_label}.exe" "${program_label}")
  if(_lx_program_path)
    lx_windows_msvc_log_result("${program_label} probe result=${_lx_program_path}")
  else()
    lx_windows_msvc_log_result("${program_label} probe result=NOT FOUND")
  endif()
endfunction()

function(lx_windows_msvc_normalize_path input_path output_var)
  if(NOT input_path)
    set(${output_var} "" PARENT_SCOPE)
    return()
  endif()

  set(_lx_path "${input_path}")
  string(REPLACE "\\" "/" _lx_path "${_lx_path}")
  string(REGEX REPLACE "/+$" "" _lx_path "${_lx_path}")
  set(${output_var} "${_lx_path}" PARENT_SCOPE)
endfunction()

function(lx_windows_msvc_map_arch input output_var)
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

function(lx_windows_msvc_read_cmd_env env_name output_var)
  execute_process(
    COMMAND cmd /d /c "echo %${env_name}%"
    OUTPUT_VARIABLE _lx_value
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _lx_result
    ERROR_QUIET
  )

  if(_lx_result EQUAL 0 AND NOT _lx_value STREQUAL "%${env_name}%")
    set(${output_var} "${_lx_value}" PARENT_SCOPE)
  else()
    set(${output_var} "" PARENT_SCOPE)
  endif()
endfunction()

function(lx_windows_msvc_check_existing_env out_ready out_reason)
  lx_windows_msvc_log_phase("check_existing_env")
  lx_windows_msvc_log_action("inspect existing Visual Studio environment variables")

  set(_lx_reason "")
  set(_lx_ready FALSE)

  lx_windows_msvc_log_detail("ENV{VCToolsInstallDir}=$ENV{VCToolsInstallDir}")
  lx_windows_msvc_log_detail("ENV{VCINSTALLDIR}=$ENV{VCINSTALLDIR}")
  lx_windows_msvc_log_detail("ENV{VSINSTALLDIR}=$ENV{VSINSTALLDIR}")
  lx_windows_msvc_log_detail("ENV{PATH}=$ENV{PATH}")

  lx_windows_msvc_log_action("probe cl.exe in current PATH")
  find_program(_lx_existing_cl NAMES cl.exe cl)
  if(_lx_existing_cl)
    lx_windows_msvc_log_detail("cl probe result=${_lx_existing_cl}")
  else()
    lx_windows_msvc_log_detail("cl probe result=NOT FOUND")
  endif()
  lx_windows_msvc_probe_program("ninja")

  if(_lx_existing_cl AND DEFINED ENV{VCToolsInstallDir} AND EXISTS "$ENV{VCToolsInstallDir}")
    set(_lx_ready TRUE)
    set(_lx_reason "VCToolsInstallDir + cl.exe")
  elseif(_lx_existing_cl AND DEFINED ENV{VCINSTALLDIR} AND EXISTS "$ENV{VCINSTALLDIR}")
    set(_lx_ready TRUE)
    set(_lx_reason "VCINSTALLDIR + cl.exe")
  endif()

  set(${out_ready} "${_lx_ready}" PARENT_SCOPE)
  set(${out_reason} "${_lx_reason}" PARENT_SCOPE)
endfunction()

function(lx_windows_msvc_collect_standard_roots output_var)
  set(_lx_roots "")

  lx_windows_msvc_read_cmd_env("ProgramFiles" _lx_program_files)
  lx_windows_msvc_read_cmd_env("ProgramFiles(x86)" _lx_program_files_x86)

  lx_windows_msvc_log_detail("ProgramFiles=${_lx_program_files}")
  lx_windows_msvc_log_detail("ProgramFiles(x86)=${_lx_program_files_x86}")

  if(_lx_program_files)
    list(APPEND _lx_roots "${_lx_program_files}/Microsoft Visual Studio")
  endif()
  if(_lx_program_files_x86)
    list(APPEND _lx_roots "${_lx_program_files_x86}/Microsoft Visual Studio")
  endif()

  list(REMOVE_DUPLICATES _lx_roots)
  set(${output_var} "${_lx_roots}" PARENT_SCOPE)
endfunction()

function(lx_windows_msvc_choose_candidate base_dir output_root output_script output_kind)
  lx_windows_msvc_normalize_path("${base_dir}" _lx_base_dir)
  lx_windows_msvc_log_action("check Visual Studio root ${_lx_base_dir}")

  if(NOT _lx_base_dir OR NOT EXISTS "${_lx_base_dir}")
    set(${output_root} "" PARENT_SCOPE)
    set(${output_script} "" PARENT_SCOPE)
    set(${output_kind} "" PARENT_SCOPE)
    return()
  endif()

  file(GLOB _lx_year_dirs LIST_DIRECTORIES true "${_lx_base_dir}/*")
  list(SORT _lx_year_dirs)
  list(REVERSE _lx_year_dirs)

  foreach(_lx_year_dir IN LISTS _lx_year_dirs)
    if(NOT IS_DIRECTORY "${_lx_year_dir}")
      continue()
    endif()

    get_filename_component(_lx_year_name "${_lx_year_dir}" NAME)
    lx_windows_msvc_log_action("check version directory ${_lx_year_name}")

    if(NOT _lx_year_name MATCHES "^[0-9]+$")
      continue()
    endif()
    if(_lx_year_name LESS 2022)
      continue()
    endif()

    foreach(_lx_sku IN ITEMS Community Professional Enterprise BuildTools Preview)
      set(_lx_candidate_root "${_lx_year_dir}/${_lx_sku}")
      lx_windows_msvc_log_action("check candidate ${_lx_candidate_root}")
      if(NOT IS_DIRECTORY "${_lx_candidate_root}")
        continue()
      endif()

      set(_lx_vsdevcmd "${_lx_candidate_root}/Common7/Tools/VsDevCmd.bat")
      set(_lx_vcvarsall "${_lx_candidate_root}/VC/Auxiliary/Build/vcvarsall.bat")
      lx_windows_msvc_log_detail("probe VsDevCmd=${_lx_vsdevcmd}")
      lx_windows_msvc_log_detail("probe vcvarsall=${_lx_vcvarsall}")

      if(EXISTS "${_lx_vsdevcmd}")
        set(${output_root} "${_lx_candidate_root}" PARENT_SCOPE)
        set(${output_script} "${_lx_vsdevcmd}" PARENT_SCOPE)
        set(${output_kind} "VsDevCmd" PARENT_SCOPE)
        return()
      endif()

      if(EXISTS "${_lx_vcvarsall}")
        set(${output_root} "${_lx_candidate_root}" PARENT_SCOPE)
        set(${output_script} "${_lx_vcvarsall}" PARENT_SCOPE)
        set(${output_kind} "vcvarsall" PARENT_SCOPE)
        return()
      endif()
    endforeach()
  endforeach()

  set(${output_root} "" PARENT_SCOPE)
  set(${output_script} "" PARENT_SCOPE)
  set(${output_kind} "" PARENT_SCOPE)
endfunction()

function(lx_windows_msvc_find_install output_root output_script output_kind)
  lx_windows_msvc_log_phase("find_install")

  if(LX_WINDOWS_VS_INSTALLATION_PATH)
    lx_windows_msvc_log_action("check explicit VS installation path")
    lx_windows_msvc_choose_candidate("${LX_WINDOWS_VS_INSTALLATION_PATH}"
      _lx_root _lx_script _lx_kind)
    if(_lx_root)
      lx_windows_msvc_log_result("Selected Visual Studio installation: ${_lx_root}")
      lx_windows_msvc_log_result("Selected bootstrap script: ${_lx_script} (${_lx_kind})")
      set(${output_root} "${_lx_root}" PARENT_SCOPE)
      set(${output_script} "${_lx_script}" PARENT_SCOPE)
      set(${output_kind} "${_lx_kind}" PARENT_SCOPE)
      return()
    endif()
    message(FATAL_ERROR
      "[windows_msvc_env] Explicit LX_WINDOWS_VS_INSTALLATION_PATH is set but no "
      "supported VS2022+ bootstrap script was found under it: "
      "${LX_WINDOWS_VS_INSTALLATION_PATH}")
  endif()

  lx_windows_msvc_log_action("scan standard Visual Studio installation roots")
  lx_windows_msvc_collect_standard_roots(_lx_standard_roots)
  foreach(_lx_root_dir IN LISTS _lx_standard_roots)
    lx_windows_msvc_choose_candidate("${_lx_root_dir}" _lx_root _lx_script _lx_kind)
    if(_lx_root)
      lx_windows_msvc_log_result("Selected Visual Studio installation: ${_lx_root}")
      lx_windows_msvc_log_result("Selected bootstrap script: ${_lx_script} (${_lx_kind})")
      set(${output_root} "${_lx_root}" PARENT_SCOPE)
      set(${output_script} "${_lx_script}" PARENT_SCOPE)
      set(${output_kind} "${_lx_kind}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  message(FATAL_ERROR
    "[windows_msvc_env] Unable to find a supported Visual Studio 2022+ installation "
    "with VsDevCmd.bat or vcvarsall.bat.")
endfunction()

function(lx_windows_msvc_import_env install_root bootstrap_script bootstrap_kind)
  lx_windows_msvc_log_phase("import_env")
  lx_windows_msvc_log_result("Using Visual Studio root: ${install_root}")
  lx_windows_msvc_log_action("determine bootstrap architecture")

  if(CMAKE_GENERATOR_PLATFORM)
    set(_lx_requested_arch "${CMAKE_GENERATOR_PLATFORM}")
  elseif(DEFINED ENV{Platform})
    set(_lx_requested_arch "$ENV{Platform}")
  elseif(CMAKE_HOST_SYSTEM_PROCESSOR)
    set(_lx_requested_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
  else()
    set(_lx_requested_arch "x64")
  endif()

  lx_windows_msvc_map_arch("${_lx_requested_arch}" _lx_target_arch)
  lx_windows_msvc_map_arch("${CMAKE_HOST_SYSTEM_PROCESSOR}" _lx_host_arch)
  lx_windows_msvc_log_result("Bootstrap arch target=${_lx_target_arch} host=${_lx_host_arch}")

  if(bootstrap_kind STREQUAL "VsDevCmd")
    set(_lx_bootstrap_args "-no_logo -arch=${_lx_target_arch} -host_arch=${_lx_host_arch}")
  else()
    set(_lx_bootstrap_args "${_lx_target_arch}")
  endif()

  set(_lx_cmd_script "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/lx_windows_msvc_env_bootstrap.cmd")
  get_filename_component(_lx_cmd_dir "${_lx_cmd_script}" DIRECTORY)
  lx_windows_msvc_normalize_path("${install_root}" _lx_bootstrap_cwd)

  lx_windows_msvc_log_action("create bootstrap command script at ${_lx_cmd_script}")
  lx_windows_msvc_log_result("Bootstrap working directory=${_lx_bootstrap_cwd}")
  file(MAKE_DIRECTORY "${_lx_cmd_dir}")
  file(WRITE "${_lx_cmd_script}"
    "@echo off\r\n"
    "cd /d \"${_lx_bootstrap_cwd}\"\r\n"
    "call \"${bootstrap_script}\" ${_lx_bootstrap_args} >nul\r\n"
    "if errorlevel 1 exit /b %errorlevel%\r\n"
    "set\r\n")

  lx_windows_msvc_log_action("execute bootstrap script ${bootstrap_script}")
  execute_process(
    COMMAND cmd /d /c "${_lx_cmd_script}"
    OUTPUT_VARIABLE _lx_env_dump
    ERROR_VARIABLE _lx_env_error
    RESULT_VARIABLE _lx_env_result
  )
  file(REMOVE "${_lx_cmd_script}")

  lx_windows_msvc_log_result("bootstrap return code=${_lx_env_result}")
  if(_lx_env_error)
    string(STRIP "${_lx_env_error}" _lx_env_error)
    lx_windows_msvc_log_detail("bootstrap stderr=${_lx_env_error}")
  endif()

  if(NOT _lx_env_result EQUAL 0 OR NOT _lx_env_dump)
    if(NOT _lx_env_error)
      set(_lx_env_error "bootstrap failed with code ${_lx_env_result}")
    endif()
    message(FATAL_ERROR
      "[windows_msvc_env] Failed to import MSVC environment via ${bootstrap_kind} "
      "at ${bootstrap_script}: ${_lx_env_error}")
  endif()

  lx_windows_msvc_log_action("parse bootstrap environment snapshot")
  string(REPLACE "\r\n" "\n" _lx_env_dump "${_lx_env_dump}")
  string(REPLACE "\r" "\n" _lx_env_dump "${_lx_env_dump}")
  string(REPLACE "\n" ";" _lx_env_lines "${_lx_env_dump}")

  set(_lx_allowed_vars
    PATH
    INCLUDE
    LIB
    LIBPATH
    VCINSTALLDIR
    VCToolsInstallDir
    VSINSTALLDIR
    WindowsSdkDir
    WindowsSdkVersion
    UCRTVersion
    UniversalCRTSdkDir)
  set(_lx_imported_vars "")
  set(_lx_seen_allowed_vars "")
  set(_lx_bootstrap_path_value "")
  set(_lx_original_path "$ENV{PATH}")

  foreach(_lx_line IN LISTS _lx_env_lines)
    if(_lx_line MATCHES "^([^=]+)=(.*)$")
      set(_lx_name "${CMAKE_MATCH_1}")
      set(_lx_value "${CMAKE_MATCH_2}")
      if(_lx_name MATCHES "^=")
        continue()
      endif()

      string(TOUPPER "${_lx_name}" _lx_name_upper)
      if(_lx_name_upper STREQUAL "PATH")
        set(_lx_bootstrap_path_value "${_lx_value}")
      endif()
      list(FIND _lx_allowed_vars "${_lx_name_upper}" _lx_allowed_index)
      if(_lx_allowed_index GREATER -1)
        lx_windows_msvc_log_action("write environment variable ${_lx_name}")
        list(APPEND _lx_seen_allowed_vars "${_lx_name}")
        list(APPEND _lx_imported_vars "${_lx_name_upper}")
        if(_lx_name_upper STREQUAL "PATH")
          continue()
        endif()
        set(ENV{${_lx_name}} "${_lx_value}")
      endif()
    endif()
  endforeach()

  if(_lx_bootstrap_path_value)
    if(_lx_original_path)
      set(_lx_merged_path "${_lx_bootstrap_path_value};${_lx_original_path}")
    else()
      set(_lx_merged_path "${_lx_bootstrap_path_value}")
    endif()
    lx_windows_msvc_log_action("write merged environment variable PATH")
    set(ENV{PATH} "${_lx_merged_path}")
  endif()

  list(REMOVE_DUPLICATES _lx_seen_allowed_vars)
  list(REMOVE_DUPLICATES _lx_imported_vars)
  string(JOIN ", " _lx_seen_allowed_summary ${_lx_seen_allowed_vars})
  string(JOIN ", " _lx_imported_summary ${_lx_imported_vars})
  if(_lx_seen_allowed_summary)
    lx_windows_msvc_log_result("Captured bootstrap variable names: ${_lx_seen_allowed_summary}")
  endif()
  lx_windows_msvc_log_result("Imported environment variables: ${_lx_imported_summary}")
  if(_lx_bootstrap_path_value)
    lx_windows_msvc_log_result("Bootstrap raw PATH=${_lx_bootstrap_path_value}")
    lx_windows_msvc_log_result("Original PATH before import=${_lx_original_path}")
  endif()
  lx_windows_msvc_log_detail("Imported PATH=$ENV{PATH}")
  lx_windows_msvc_probe_program("ninja")
endfunction()

function(lx_windows_msvc_finalize_compiler)
  lx_windows_msvc_log_phase("finalize_compiler")
  lx_windows_msvc_log_action("probe cl.exe after environment import")
  find_program(_lx_cl_compiler NAMES cl.exe cl)

  if(NOT _lx_cl_compiler)
    message(FATAL_ERROR
      "[windows_msvc_env] Imported the Visual Studio environment, but cl.exe was "
      "not found afterwards. PATH=$ENV{PATH}")
  endif()

  set(CMAKE_C_COMPILER "${_lx_cl_compiler}" CACHE FILEPATH
      "MSVC C compiler imported from Visual Studio developer environment" FORCE)
  set(CMAKE_CXX_COMPILER "${_lx_cl_compiler}" CACHE FILEPATH
      "MSVC CXX compiler imported from Visual Studio developer environment" FORCE)
  lx_windows_msvc_probe_program("ninja")
  if(DEFINED CMAKE_MAKE_PROGRAM AND CMAKE_MAKE_PROGRAM)
    lx_windows_msvc_log_result("CMAKE_MAKE_PROGRAM preset=${CMAKE_MAKE_PROGRAM}")
  else()
    lx_windows_msvc_log_result("CMAKE_MAKE_PROGRAM preset=<empty>")
  endif()
  lx_windows_msvc_log_result("Configured compiler from imported environment: ${_lx_cl_compiler}")
endfunction()
