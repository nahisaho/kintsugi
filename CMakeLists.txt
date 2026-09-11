cmake_minimum_required(VERSION 3.16)
project(kintsugi_pottery_reassembly CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(pottery_core
  src/core/pose_conflict.cpp
)
target_include_directories(pottery_core PUBLIC src)

add_executable(pottery_tests
  tests/test_main.cpp
  tests/test_pose_conflict.cpp
)
target_include_directories(pottery_tests PRIVATE third_party src)
target_link_libraries(pottery_tests PRIVATE pottery_core)
