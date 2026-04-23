function(lx_windows_msvc_log_detail message_text)
  if(LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV)
    message(STATUS "[windows_msvc_env] ${message_text}")
  endif()
endfunction()

function(lx_windows_msvc_log_phase phase_name)
  lx_windows_msvc_log_result("phase: ${phase_name}")
endfunction()

function(lx_windows_msvc_log_result result_text)
  message(STATUS "[windows_msvc_env] ${result_text}")
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
  lx_windows_msvc_log_detail("checking current environment")
  lx_windows_msvc_log_detail("PATH=$ENV{PATH}")
  lx_windows_msvc_log_detail("VCINSTALLDIR=$ENV{VCINSTALLDIR}")
  lx_windows_msvc_log_detail("VCTOOLSINSTALLDIR=$ENV{VCTOOLSINSTALLDIR}")

  find_program(_lx_existing_cl NAMES cl.exe cl)
  if(_lx_existing_cl AND DEFINED ENV{VCTOOLSINSTALLDIR} AND EXISTS "$ENV{VCTOOLSINSTALLDIR}")
    set(${out_ready} TRUE PARENT_SCOPE)
  elseif(_lx_existing_cl AND DEFINED ENV{VCINSTALLDIR} AND EXISTS "$ENV{VCINSTALLDIR}")
    set(${out_ready} TRUE PARENT_SCOPE)
  else()
    set(${out_ready} FALSE PARENT_SCOPE)
  endif()

  if(LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV)
    lx_windows_msvc_probe_program("cl")
    lx_windows_msvc_probe_program("link")
    lx_windows_msvc_probe_program("rc")
    lx_windows_msvc_probe_program("mt")
    lx_windows_msvc_probe_program("ninja")
  endif()
endfunction()

function(lx_windows_msvc_collect_standard_roots output_var)
  set(_lx_roots "")
  lx_windows_msvc_read_cmd_env("ProgramFiles" _lx_program_files)
  lx_windows_msvc_read_cmd_env("ProgramFiles(x86)" _lx_program_files_x86)

  if(_lx_program_files)
    list(APPEND _lx_roots "${_lx_program_files}/Microsoft Visual Studio")
  endif()
  if(_lx_program_files_x86)
    list(APPEND _lx_roots "${_lx_program_files_x86}/Microsoft Visual Studio")
  endif()

  list(REMOVE_DUPLICATES _lx_roots)
  set(${output_var} "${_lx_roots}" PARENT_SCOPE)
endfunction()

function(lx_windows_msvc_choose_candidate base_dir output_root output_script)
  lx_windows_msvc_normalize_path("${base_dir}" _lx_base_dir)
  if(NOT _lx_base_dir OR NOT EXISTS "${_lx_base_dir}")
    set(${output_root} "" PARENT_SCOPE)
    set(${output_script} "" PARENT_SCOPE)
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
    if(NOT _lx_year_name MATCHES "^[0-9]+$")
      continue()
    endif()
    if(_lx_year_name LESS 2022)
      continue()
    endif()

    foreach(_lx_sku IN ITEMS Community Professional Enterprise BuildTools Preview)
      set(_lx_candidate_root "${_lx_year_dir}/${_lx_sku}")
      if(NOT IS_DIRECTORY "${_lx_candidate_root}")
        continue()
      endif()

      set(_lx_vsdevcmd "${_lx_candidate_root}/Common7/Tools/VsDevCmd.bat")
      if(EXISTS "${_lx_vsdevcmd}")
        set(${output_root} "${_lx_candidate_root}" PARENT_SCOPE)
        set(${output_script} "${_lx_vsdevcmd}" PARENT_SCOPE)
        return()
      endif()
    endforeach()
  endforeach()

  set(${output_root} "" PARENT_SCOPE)
  set(${output_script} "" PARENT_SCOPE)
endfunction()

function(lx_windows_msvc_find_install output_root output_script)
  if(LX_WINDOWS_VS_INSTALLATION_PATH)
    lx_windows_msvc_log_detail("checking explicit path ${LX_WINDOWS_VS_INSTALLATION_PATH}")
    lx_windows_msvc_normalize_path("${LX_WINDOWS_VS_INSTALLATION_PATH}" _lx_explicit_root)
    if(_lx_explicit_root AND EXISTS "${_lx_explicit_root}/Common7/Tools/VsDevCmd.bat")
      set(${output_root} "${_lx_explicit_root}" PARENT_SCOPE)
      set(${output_script} "${_lx_explicit_root}/Common7/Tools/VsDevCmd.bat" PARENT_SCOPE)
      return()
    endif()
    lx_windows_msvc_choose_candidate("${LX_WINDOWS_VS_INSTALLATION_PATH}" _lx_root _lx_script)
    if(_lx_root)
      set(${output_root} "${_lx_root}" PARENT_SCOPE)
      set(${output_script} "${_lx_script}" PARENT_SCOPE)
      return()
    endif()
    message(FATAL_ERROR
      "[windows_msvc_env] Explicit LX_WINDOWS_VS_INSTALLATION_PATH does not contain "
      "a usable VsDevCmd.bat: ${LX_WINDOWS_VS_INSTALLATION_PATH}")
  endif()

  lx_windows_msvc_collect_standard_roots(_lx_standard_roots)
  foreach(_lx_root_dir IN LISTS _lx_standard_roots)
    lx_windows_msvc_log_detail("checking standard root ${_lx_root_dir}")
    lx_windows_msvc_choose_candidate("${_lx_root_dir}" _lx_root _lx_script)
    if(_lx_root)
      set(${output_root} "${_lx_root}" PARENT_SCOPE)
      set(${output_script} "${_lx_script}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  message(FATAL_ERROR
    "[windows_msvc_env] Unable to find VsDevCmd.bat under a supported Visual Studio "
    "2022+ installation.")
endfunction()

function(lx_windows_msvc_import_env install_root bootstrap_script)
  lx_windows_msvc_log_result("Using Visual Studio root: ${install_root}")
  lx_windows_msvc_log_result("Using VsDevCmd: ${bootstrap_script}")

  set(_lx_cmd_script "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/lx_windows_msvc_env_bootstrap.cmd")
  set(_lx_gen_cmake "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/lx_windows_msvc_env.gen.cmake")
  set(_lx_log_file "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/lx_windows_msvc_env.log")
  get_filename_component(_lx_cmd_dir "${_lx_cmd_script}" DIRECTORY)
  lx_windows_msvc_normalize_path("${install_root}" _lx_bootstrap_cwd)
  set(_lx_env_begin_marker "__LX_ENV_SNAPSHOT_BEGIN__")
  set(_lx_env_end_marker "__LX_ENV_SNAPSHOT_END__")

  file(MAKE_DIRECTORY "${_lx_cmd_dir}")

  set(_lx_debug_line "")
  if(LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV)
    set(_lx_debug_line "set VSCMD_DEBUG=3\r\n")
  endif()

  lx_windows_msvc_log_result("Bootstrap working directory=${_lx_bootstrap_cwd}")
  file(WRITE "${_lx_cmd_script}"
    "@echo off\r\n"
    "cd /d \"${_lx_bootstrap_cwd}\"\r\n"
    "set VSCMD_SKIP_SENDTELEMETRY=1\r\n"
    "${_lx_debug_line}"
    "call \"${bootstrap_script}\"\r\n"
    "if errorlevel 1 exit /b %errorlevel%\r\n"
    "echo ${_lx_env_begin_marker}\r\n"
    "set\r\n"
    "echo ${_lx_env_end_marker}\r\n")

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
      --cmd-script "${_lx_cmd_script}"
      --output-cmake "${_lx_gen_cmake}"
      --output-log "${_lx_log_file}"
      --env-begin-marker "${_lx_env_begin_marker}"
      --env-end-marker "${_lx_env_end_marker}"
    OUTPUT_VARIABLE _lx_python_output
    ERROR_VARIABLE _lx_python_error
    RESULT_VARIABLE _lx_python_result
  )
  file(REMOVE "${_lx_cmd_script}")

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

  if(LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV AND EXISTS "${_lx_log_file}")
    file(READ "${_lx_log_file}" _lx_log_dump)
    string(REPLACE "\r\n" "\n" _lx_log_dump "${_lx_log_dump}")
    string(REPLACE "\r" "\n" _lx_log_dump "${_lx_log_dump}")
    string(REPLACE ";" "\\;" _lx_log_dump "${_lx_log_dump}")
    string(REPLACE "\n" ";" _lx_log_lines "${_lx_log_dump}")
    lx_windows_msvc_log_result("VsDevCmd output follows:")
    foreach(_lx_log_line IN LISTS _lx_log_lines)
      lx_windows_msvc_log_detail("${_lx_log_line}")
    endforeach()
  endif()

  lx_windows_msvc_log_result("Imported PATH=$ENV{PATH}")
  lx_windows_msvc_log_result("Imported LIB=$ENV{LIB}")
  lx_windows_msvc_log_result("Imported INCLUDE=$ENV{INCLUDE}")
  lx_windows_msvc_log_result("Imported LIBPATH=$ENV{LIBPATH}")
  lx_windows_msvc_log_result("Imported VCINSTALLDIR=$ENV{VCINSTALLDIR}")
  lx_windows_msvc_log_result("Imported VCTOOLSINSTALLDIR=$ENV{VCTOOLSINSTALLDIR}")
  lx_windows_msvc_log_result("Imported WINDOWSSDKDIR=$ENV{WINDOWSSDKDIR}")
  lx_windows_msvc_log_result("Imported WINDOWSSDKVERSION=$ENV{WINDOWSSDKVERSION}")

  if(LX_CMAKE_DEBUG_WINDOWS_MSVC_ENV)
    lx_windows_msvc_probe_program("cl")
    lx_windows_msvc_probe_program("link")
    lx_windows_msvc_probe_program("rc")
    lx_windows_msvc_probe_program("mt")
    lx_windows_msvc_probe_program("ninja")
  endif()
endfunction()
