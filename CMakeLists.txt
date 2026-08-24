cmake_minimum_required(VERSION 3.20)
project(sprite_splitter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# ---------- core 静态库（零依赖，不依赖 Godot/stdio/网络） ----------
add_library(sps_core STATIC
  core/analyzer.cpp
  core/image/image.cpp
  core/mask/mask.cpp
  core/mask/mask_io.cpp
  core/mask/morphology.cpp
  core/segmentation/background.cpp
  core/segmentation/connected_components.cpp
  core/segmentation/grid_detector.cpp
  core/segmentation/splitter.cpp
  core/export/png_exporter.cpp
  core/export/json_exporter.cpp
  core/export/sheet.cpp
)
target_include_directories(sps_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_include_directories(sps_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
                                        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json)

# ---------- CLI ----------
add_executable(sprite-split cli/main.cpp)
target_link_libraries(sprite-split PRIVATE sps_core)
target_include_directories(sprite-split PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json)

# ---------- 测试（Catch2 单头版） ----------
enable_testing()
add_executable(unit_tests
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/catch2/catch_amalgamated.cpp
  tests/test_analyzer.cpp
  tests/test_image.cpp
  tests/test_mask.cpp
  tests/test_mask_io.cpp
  tests/test_components.cpp
  tests/test_splitter.cpp
  tests/test_background.cpp
  tests/test_grid.cpp
  tests/test_morphology.cpp
  tests/test_json.cpp
  tests/test_sheet.cpp
)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/catch2)
target_link_libraries(unit_tests PRIVATE sps_core)

add_test(NAME unit_tests COMMAND unit_tests)
