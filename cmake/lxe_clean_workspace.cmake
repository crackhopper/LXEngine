if(POLICY CMP0009)
  cmake_policy(SET CMP0009 NEW)
endif()

if(NOT DEFINED LXE_SOURCE_DIR)
  message(FATAL_ERROR "LXE_SOURCE_DIR is required")
endif()

file(TO_CMAKE_PATH "${LXE_SOURCE_DIR}" LXE_SOURCE_DIR)

set(_lxe_clean_paths
  "${LXE_SOURCE_DIR}/artifacts"
  "${LXE_SOURCE_DIR}/.pytest_cache"
  "${LXE_SOURCE_DIR}/.mypy_cache"
  "${LXE_SOURCE_DIR}/.ruff_cache"
)

file(GLOB _lxe_tmp_paths
  LIST_DIRECTORIES true
  "${LXE_SOURCE_DIR}/tmp_*"
)
list(APPEND _lxe_clean_paths ${_lxe_tmp_paths})

file(GLOB_RECURSE _lxe_source_spv
  "${LXE_SOURCE_DIR}/assets/shaders/*.spv"
)
list(APPEND _lxe_clean_paths ${_lxe_source_spv})

foreach(_root IN ITEMS src tests scripts)
  if(EXISTS "${LXE_SOURCE_DIR}/${_root}")
    file(GLOB _lxe_pycache_dirs
      LIST_DIRECTORIES true
      "${LXE_SOURCE_DIR}/${_root}/__pycache__"
      "${LXE_SOURCE_DIR}/${_root}/*/__pycache__"
      "${LXE_SOURCE_DIR}/${_root}/*/*/__pycache__"
      "${LXE_SOURCE_DIR}/${_root}/*/*/*/__pycache__"
      "${LXE_SOURCE_DIR}/${_root}/*/*/*/*/__pycache__"
    )
    file(GLOB _lxe_pyc_files
      "${LXE_SOURCE_DIR}/${_root}/*.pyc"
      "${LXE_SOURCE_DIR}/${_root}/*/*.pyc"
      "${LXE_SOURCE_DIR}/${_root}/*/*/*.pyc"
      "${LXE_SOURCE_DIR}/${_root}/*/*/*/*.pyc"
      "${LXE_SOURCE_DIR}/${_root}/*/*/*/*/*.pyc"
    )
    list(APPEND _lxe_clean_paths ${_lxe_pycache_dirs} ${_lxe_pyc_files})
  endif()
endforeach()

foreach(_path IN LISTS _lxe_clean_paths)
  if(EXISTS "${_path}")
    file(REMOVE_RECURSE "${_path}")
    message(STATUS "removed ${_path}")
  endif()
endforeach()
