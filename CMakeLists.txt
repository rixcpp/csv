cmake_minimum_required(VERSION 3.20)

project(rix_csv
  VERSION 0.1.0
  DESCRIPTION "Small CSV reader and writer for Vix C++ projects"
  LANGUAGES CXX
)

option(RIX_CSV_BUILD_EXAMPLES "Build rix/csv examples" ON)
option(RIX_CSV_BUILD_TESTS "Build rix/csv tests" ON)

add_library(rix_csv INTERFACE)
add_library(rix::csv ALIAS rix_csv)

target_compile_features(rix_csv INTERFACE cxx_std_20)

target_include_directories(rix_csv
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  if(RIX_CSV_BUILD_EXAMPLES)
    add_subdirectory(examples)
  endif()

  if(RIX_CSV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()
endif()
