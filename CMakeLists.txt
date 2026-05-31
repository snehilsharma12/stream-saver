cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES CXX)

option(STREAM_SAVER_BUILD_TESTS "Build Stream Saver unit tests" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin.cpp
    src/stream_saver_filter.cpp
    src/ocr_client.cpp
    src/worker_process.cpp
    src/matcher.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(WIN32)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

install(
  DIRECTORY worker/
  DESTINATION "${CMAKE_PROJECT_NAME}/data/worker"
  USE_SOURCE_PERMISSIONS
  PATTERN "__pycache__" EXCLUDE
  PATTERN "*.pyc" EXCLUDE
)

if(STREAM_SAVER_BUILD_TESTS)
  enable_testing()

  add_executable(stream-saver-matcher-tests tests/cpp/test_matcher.cpp src/matcher.cpp)
  target_include_directories(stream-saver-matcher-tests PRIVATE src)
  target_compile_features(stream-saver-matcher-tests PRIVATE cxx_std_17)
  add_test(NAME matcher COMMAND stream-saver-matcher-tests)
endif()
