cmake_minimum_required(VERSION 3.16)
# C言語も有効化する: PCL/VTKのfind_package内部でMPI(C)検出が行われるため。
project(kintsugi_pottery_reassembly CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PCL 1.14 REQUIRED COMPONENTS common io registration kdtree search)
include_directories(${PCL_INCLUDE_DIRS})
link_directories(${PCL_LIBRARY_DIRS})
add_definitions(${PCL_DEFINITIONS})

find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MINIZIP REQUIRED minizip)

add_library(pottery_core
  src/core/pose_conflict.cpp
  src/core/scan_importer.cpp
  src/core/fragment_clustering.cpp
  src/core/fragment_matching.cpp
  src/core/assembly_orchestrator.cpp
  src/core/missing_part_estimation.cpp
  src/core/report_aggregation.cpp
  src/core/mesh_export.cpp
  src/core/project_persistence.cpp
  src/core/offline_execution.cpp
  src/gui/viewport_camera.cpp
  src/gui/gui_command_dispatcher.cpp
)
target_include_directories(pottery_core PUBLIC src)
target_include_directories(pottery_core PRIVATE ${MINIZIP_INCLUDE_DIRS})
target_link_libraries(pottery_core PUBLIC ${PCL_LIBRARIES})
target_link_libraries(pottery_core PRIVATE OpenSSL::Crypto ${MINIZIP_LIBRARIES})

add_executable(pottery_tests
  tests/test_main.cpp
  tests/test_pose_conflict.cpp
  tests/test_scan_importer.cpp
  tests/test_fragment_clustering.cpp
  tests/test_fragment_matching.cpp
  tests/test_assembly_orchestrator.cpp
  tests/test_missing_part_estimation.cpp
  tests/test_report_aggregation.cpp
  tests/test_mesh_export.cpp
  tests/test_viewport_camera.cpp
  tests/test_gui_command_dispatcher.cpp
  tests/test_project_persistence.cpp
  tests/test_offline_execution.cpp
)
target_include_directories(pottery_tests PRIVATE third_party src)
target_link_libraries(pottery_tests PRIVATE pottery_core)
target_compile_definitions(pottery_tests PRIVATE KINTSUGI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
