cmake_minimum_required(VERSION 3.24)
project(Himaya VERSION 0.7.0 LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Himaya currently targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SQLITE_VERSION "3540100")
set(SQLITE_ROOT "${CMAKE_BINARY_DIR}/_deps/sqlite")
set(SQLITE_ARCHIVE "${CMAKE_BINARY_DIR}/_deps/sqlite-amalgamation-${SQLITE_VERSION}.zip")
set(SQLITE_SOURCE_DIR "${SQLITE_ROOT}/sqlite-amalgamation-${SQLITE_VERSION}")

if(NOT EXISTS "${SQLITE_SOURCE_DIR}/sqlite3.c")
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")
  file(DOWNLOAD
    "https://www.sqlite.org/2026/sqlite-amalgamation-${SQLITE_VERSION}.zip"
    "${SQLITE_ARCHIVE}"
    TLS_VERIFY ON SHOW_PROGRESS STATUS SQLITE_DOWNLOAD_STATUS)
  list(GET SQLITE_DOWNLOAD_STATUS 0 SQLITE_DOWNLOAD_CODE)
  if(NOT SQLITE_DOWNLOAD_CODE EQUAL 0)
    list(GET SQLITE_DOWNLOAD_STATUS 1 SQLITE_DOWNLOAD_MESSAGE)
    message(FATAL_ERROR "Could not download SQLite: ${SQLITE_DOWNLOAD_MESSAGE}")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${SQLITE_ARCHIVE}" DESTINATION "${SQLITE_ROOT}")
endif()

add_library(HimayaCore STATIC
  Core/ConfigStore.cpp
  Core/WebsiteBlocker.cpp
  Core/ExecutionPolicy.cpp
  Core/AppLockerPolicy.cpp
  "${SQLITE_SOURCE_DIR}/sqlite3.c")
target_include_directories(HimayaCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${SQLITE_SOURCE_DIR}")
target_compile_definitions(HimayaCore PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
target_link_libraries(HimayaCore PUBLIC shell32 shlwapi ole32 winhttp normaliz advapi32 wtsapi32)
if(MSVC)
  target_compile_options(HimayaCore PRIVATE /W4 /permissive- /utf-8)
  set_source_files_properties("${SQLITE_SOURCE_DIR}/sqlite3.c" PROPERTIES COMPILE_OPTIONS "/w")
endif()

add_executable(Himaya WIN32 App/main.cpp App/MainWindow.cpp)
target_link_libraries(Himaya PRIVATE HimayaCore comctl32 comdlg32 dwmapi uxtheme)
if(MSVC)
  target_link_options(Himaya PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()
add_executable(HimayaGuard Service/service_main.cpp)
target_link_libraries(HimayaGuard PRIVATE HimayaCore advapi32)

add_executable(HimayaPolicySmokeTest tests/PolicySmokeTest.cpp)
target_link_libraries(HimayaPolicySmokeTest PRIVATE HimayaCore advapi32)

set_target_properties(Himaya PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
set_target_properties(HimayaGuard PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
set_target_properties(HimayaPolicySmokeTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
