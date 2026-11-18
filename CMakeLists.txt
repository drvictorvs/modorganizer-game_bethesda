# Standalone build of MO2's Starfield game plugin (game_starfield.dll) for
# Mod Organizer 2.5.2.
#
# Needs: MSVC (Visual Studio 2022 or 2026), Qt 6.7.x "MSVC 64-bit" and an
# installed MO2 2.5.2. Everything else (uibase headers, zlib, lz4) is in third_party/.
#
# Cache variables:
#   MO2_DIR  - your MO2 2.5.2 folder (contains ModOrganizer.exe and uibase.dll);
#              guessed from the nxm:// handler when left empty
#   QT_ROOT  - Qt 6.7.x MSVC folder, default C:/Qt/6.7.1/msvc2019_64

cmake_minimum_required(VERSION 3.24)
project(game_starfield_mo2_252 LANGUAGES C CXX)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Build with 64-bit MSVC (MO2 plugins must match MO2's ABI).")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Mo2Uibase)

# --- Mod Organizer 2.5.2 install --------------------------------------------------
set(MO2_DIR "" CACHE PATH "Mod Organizer 2.5.2 folder (contains uibase.dll)")
set(MO2_SKIP_VERSION_CHECK OFF CACHE BOOL "Build even if ModOrganizer.exe is not 2.5.2")

if(NOT MO2_DIR)
  mo2_guess_install_dir(guessed)
  if(guessed)
    set(MO2_DIR "${guessed}" CACHE PATH "Mod Organizer 2.5.2 folder (contains uibase.dll)" FORCE)
    message(STATUS "MO2_DIR not set; using the MO2 registered for nxm:// links: ${MO2_DIR}")
  endif()
endif()
if(NOT EXISTS "${MO2_DIR}/uibase.dll")
  message(FATAL_ERROR
    "MO2_DIR must point to your Mod Organizer 2.5.2 folder (the one with "
    "ModOrganizer.exe and uibase.dll). Current value: '${MO2_DIR}'.\n"
    "VS Code: add  \"cmake.configureSettings\": { \"MO2_DIR\": \"C:/path/to/MO2\" }  "
    "to settings.json; command line: -DMO2_DIR=C:/path/to/MO2")
endif()

mo2_read_version("${MO2_DIR}" MO2_EXE_VERSION)
if(MO2_EXE_VERSION MATCHES "^2\\.5\\.2")
  message(STATUS "Mod Organizer ${MO2_EXE_VERSION} at ${MO2_DIR}")
elseif(MO2_SKIP_VERSION_CHECK)
  message(WARNING "ModOrganizer.exe reports '${MO2_EXE_VERSION}', not 2.5.2; continuing because MO2_SKIP_VERSION_CHECK is ON.")
else()
  message(FATAL_ERROR
    "ModOrganizer.exe in ${MO2_DIR} reports version '${MO2_EXE_VERSION}', but these "
    "sources and headers are MO2 2.5.2's. A plugin built for another version can "
    "crash MO2. Point MO2_DIR at a 2.5.2 install (or set MO2_SKIP_VERSION_CHECK=ON "
    "if the version simply could not be read).")
endif()

# --- Qt 6.7 (MO2 2.5.2 ships Qt 6.7.1) --------------------------------------------
set(QT_ROOT "C:/Qt/6.7.1/msvc2019_64" CACHE PATH "Qt 6.7.x MSVC 64-bit folder")
list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
find_package(Qt6 6.7 QUIET COMPONENTS Core Gui Widgets Network)
if(NOT Qt6_FOUND)
  message(FATAL_ERROR
    "Qt 6.7 (MSVC 64-bit) not found at QT_ROOT='${QT_ROOT}'. Install it with:\n"
    "    py -m pip install aqtinstall\n"
    "    py -m aqt install-qt windows desktop 6.7.1 win64_msvc2019_64 --archives qtbase -O C:\\Qt\n"
    "or set QT_ROOT to an existing Qt 6.7.x msvc2019_64 folder.")
endif()
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
  message(FATAL_ERROR
    "Found Qt ${Qt6_VERSION} at ${Qt6_DIR}. MO2 2.5.2 runs Qt 6.7.1 and refuses "
    "plugins built with a newer Qt minor version; use Qt 6.7.x.")
endif()
message(STATUS "Qt ${Qt6_VERSION}: ${Qt6_DIR}")

# --- build settings ----------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# MO2 is a release build: always use the release CRT (/MD) and release Qt, even if
# the IDE builds the "Debug" configuration, so the DLL can always be loaded.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# Point the Debug configuration at Qt's release libraries (MO2 ships Qt6Core.dll,
# not Qt6Cored.dll). Only the Qt targets the plugin links get this mapping.
get_target_property(qt_configs Qt6::Core IMPORTED_CONFIGURATIONS)
set(qt_release_configs ${qt_configs})
list(REMOVE_ITEM qt_release_configs DEBUG)
if(NOT qt_release_configs)
  message(FATAL_ERROR "Qt at ${Qt6_DIR} has no release configuration (${qt_configs}).")
endif()
foreach(qt_target Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
  set_property(TARGET ${qt_target} PROPERTY MAP_IMPORTED_CONFIG_DEBUG ${qt_release_configs})
endforeach()

mo2_import_uibase("${MO2_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/uibase-2.5.2/include")
target_link_libraries(uibase INTERFACE Qt6::Widgets Qt6::Network)

# compile settings of cmake_common's mo2_configure_target (mid-2024)
function(mo2_flags target)
  target_compile_options(${target} PRIVATE /MP /permissive- /W3 /wd4464 /utf-8
    $<$<CONFIG:RelWithDebInfo>:/O2>)
  target_compile_definitions(${target} PRIVATE
    _UNICODE UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    QT_MESSAGELOGCONTEXT
    QT_NO_DEBUG # release plugin metadata in every configuration
    # keep working with older msvcp140.dll (VS 17.10+ constexpr std::mutex change)
    _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
endfunction()

# --- zlib and lz4 (static, vendored) -----------------------------------------------
file(GLOB ZLIB_SOURCES third_party/zlib/*.c)
add_library(mo2_zlib STATIC ${ZLIB_SOURCES})
target_include_directories(mo2_zlib PUBLIC third_party/zlib)
target_compile_definitions(mo2_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)

add_library(mo2_lz4 STATIC third_party/lz4/lz4.c)
target_include_directories(mo2_lz4 PUBLIC third_party/lz4)

# --- game_gamebryo (static) ----------------------------------------------------------
file(GLOB GAMEBRYO_SOURCES CONFIGURE_DEPENDS
  src/gamebryo/*.cpp src/gamebryo/*.h src/gamebryo/*.ui)
add_library(game_gamebryo STATIC ${GAMEBRYO_SOURCES})
target_include_directories(game_gamebryo PUBLIC src/gamebryo)
target_link_libraries(game_gamebryo
  PUBLIC uibase Version Dbghelp Shell32 Ole32 Advapi32 User32
  PRIVATE mo2_zlib mo2_lz4)
mo2_flags(game_gamebryo)

# --- game_creation (static) ----------------------------------------------------------
file(GLOB CREATION_SOURCES CONFIGURE_DEPENDS src/creation/*.cpp src/creation/*.h)
add_library(game_creation STATIC ${CREATION_SOURCES})
target_include_directories(game_creation PUBLIC src/creation)
target_link_libraries(game_creation PUBLIC game_gamebryo uibase PRIVATE mo2_lz4)
mo2_flags(game_creation)

# --- game_starfield (the plugin DLL) -------------------------------------------------
file(GLOB STARFIELD_SOURCES CONFIGURE_DEPENDS
  src/starfield/*.cpp src/starfield/*.h src/starfield/*.json)
add_library(game_starfield SHARED ${STARFIELD_SOURCES})
target_include_directories(game_starfield PRIVATE src/starfield)
target_link_libraries(game_starfield PRIVATE game_creation game_gamebryo uibase
  mo2_zlib mo2_lz4)
mo2_flags(game_starfield)
target_link_options(game_starfield PRIVATE /INCREMENTAL:NO /OPT:REF /OPT:ICF)

# --- check the DLL against the DLLs of this MO2 install ------------------------------
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import pefile"
    RESULT_VARIABLE pefile_rc OUTPUT_QUIET ERROR_QUIET)
endif()
if(Python3_FOUND AND pefile_rc EQUAL 0)
  add_custom_command(TARGET game_starfield POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/ci/verify_imports.py"
            "$<TARGET_FILE:game_starfield>" "${MO2_DIR}"
            "${CMAKE_BINARY_DIR}/verify-report.txt"
    COMMENT "Checking game_starfield.dll against ${MO2_DIR}"
    VERBATIM)
else()
  message(WARNING "Python 'pefile' not available: the post-build check against "
                  "MO2's DLLs is skipped (py -m pip install pefile, then reconfigure).")
endif()

install(TARGETS game_starfield RUNTIME DESTINATION plugins)
install(FILES $<TARGET_PDB_FILE:game_starfield> DESTINATION pdb OPTIONAL)