cmake_minimum_required(VERSION 3.24)
project(Himaya VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Himaya currently targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Himaya WIN32
  App/main.cpp
  App/MainWindow.cpp
  Core/ConfigStore.cpp
)
target_include_directories(Himaya PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(Himaya PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(Himaya PRIVATE comctl32 shell32 shlwapi ole32)

if(MSVC)
  target_compile_options(Himaya PRIVATE /W4 /permissive- /utf-8)
endif()

set_target_properties(Himaya PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
