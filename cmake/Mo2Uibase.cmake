# Helpers to build against the uibase.dll of an installed Mod Organizer 2.

# Extracts exported symbol names from the text printed by `link /dump /exports`
# (same output as dumpbin /exports).
function(mo2_parse_exports dump_text out_var)
  string(REPLACE "\r" "" dump_text "${dump_text}")
  string(REPLACE ";" "" dump_text "${dump_text}")
  string(REPLACE "\n" ";" lines "${dump_text}")
  set(names)
  foreach(line IN LISTS lines)
    # "    ordinal hint RVA      name"
    if(line MATCHES "^ +[0-9]+ +[0-9A-Fa-f]+ +[0-9A-Fa-f]+ +([^ ]+)")
      list(APPEND names "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  set(${out_var} "${names}" PARENT_SCOPE)
endfunction()

# Tries to locate the MO2 folder from the nxm:// handler MO2 registers.
function(mo2_guess_install_dir out_var)
  set(${out_var} "" PARENT_SCOPE)
  if(NOT CMAKE_HOST_WIN32)
    return()
  endif()
  cmake_host_system_information(RESULT cmd
    QUERY WINDOWS_REGISTRY "HKCU/Software/Classes/nxm/shell/open/command"
    ERROR_VARIABLE err)
  if(cmd MATCHES "^\"?([^\"]*)[\\/]nxmhandler\\.exe")
    string(REPLACE "\\" "/" dir "${CMAKE_MATCH_1}")
    if(EXISTS "${dir}/uibase.dll")
      set(${out_var} "${dir}" PARENT_SCOPE)
    endif()
  endif()
endfunction()

# Reads ModOrganizer.exe's product version (empty if it cannot be read).
function(mo2_read_version mo2_dir out_var)
  set(ENV{MO2_EXE_PATH} "${mo2_dir}/ModOrganizer.exe")
  execute_process(
    COMMAND powershell -NoProfile -NonInteractive -Command
      "(Get-Item -LiteralPath $env:MO2_EXE_PATH).VersionInfo.ProductVersion"
    OUTPUT_VARIABLE version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  set(${out_var} "${version}" PARENT_SCOPE)
endfunction()

# Generates an import library for <mo2_dir>/uibase.dll in the build folder and
# creates the imported target `uibase`.
function(mo2_import_uibase mo2_dir include_dir)
  get_filename_component(msvc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
  # NO_DEFAULT_PATH: Git for Windows also ships a (different) link.exe
  find_program(MO2_MSVC_LINK link HINTS "${msvc_bin}" NO_DEFAULT_PATH REQUIRED)
  find_program(MO2_MSVC_LIB lib HINTS "${msvc_bin}" NO_DEFAULT_PATH REQUIRED)

  set(dll "${mo2_dir}/uibase.dll")
  set(def "${CMAKE_BINARY_DIR}/uibase.def")
  set(implib "${CMAKE_BINARY_DIR}/uibase.lib")

  execute_process(
    COMMAND "${MO2_MSVC_LINK}" /dump /nologo /exports "${dll}"
    OUTPUT_VARIABLE dump
    RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Could not read the exports of ${dll} (link /dump returned ${rc}).")
  endif()

  mo2_parse_exports("${dump}" names)
  list(LENGTH names count)
  if(count LESS 100)
    message(FATAL_ERROR "Only ${count} exports parsed from ${dll}; is this really MO2's uibase.dll?")
  endif()

  list(JOIN names "\n    " body)
  file(WRITE "${def}" "LIBRARY uibase\nEXPORTS\n    ${body}\n")

  execute_process(
    COMMAND "${MO2_MSVC_LIB}" /nologo "/def:${def}" /machine:x64 "/out:${implib}"
    RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "lib.exe failed to create ${implib} (${rc}).")
  endif()
  message(STATUS "uibase.lib generated from ${dll} (${count} exports)")

  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dll}")

  add_library(uibase SHARED IMPORTED GLOBAL)
  set_target_properties(uibase PROPERTIES
    IMPORTED_IMPLIB "${implib}"
    IMPORTED_LOCATION "${dll}"
    INTERFACE_INCLUDE_DIRECTORIES "${include_dir};${include_dir}/game_features")
  # Debug is mapped to these via CMAKE_MAP_IMPORTED_CONFIG_DEBUG, which only looks at
  # per-configuration properties
  set_property(TARGET uibase APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO RELEASE)
  foreach(cfg RELWITHDEBINFO RELEASE)
    set_target_properties(uibase PROPERTIES
      IMPORTED_IMPLIB_${cfg} "${implib}"
      IMPORTED_LOCATION_${cfg} "${dll}")
  endforeach()
endfunction()
