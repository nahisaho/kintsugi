cmake_minimum_required(VERSION 3.16)
# C言語も有効化する: PCL/VTKのfind_package内部でMPI(C)検出が行われるため。
project(kintsugi_pottery_reassembly CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PCL 1.14 REQUIRED COMPONENTS common io registration kdtree search)
include_directories(${PCL_INCLUDE_DIRS})
link_directories(${PCL_LIBRARY_DIRS})
add_definitions(${PCL_DEFINITIONS})

add_library(pottery_core
  src/core/pose_conflict.cpp
  src/core/scan_importer.cpp
  src/core/fragment_clustering.cpp
  src/core/fragment_matching.cpp
  src/core/assembly_orchestrator.cpp
)
target_include_directories(pottery_core PUBLIC src)
target_link_libraries(pottery_core PUBLIC ${PCL_LIBRARIES})

add_executable(pottery_tests
  tests/test_main.cpp
  tests/test_pose_conflict.cpp
  tests/test_scan_importer.cpp
  tests/test_fragment_clustering.cpp
  tests/test_fragment_matching.cpp
  tests/test_assembly_orchestrator.cpp
)
target_include_directories(pottery_tests PRIVATE third_party src)
target_link_libraries(pottery_tests PRIVATE pottery_core)
