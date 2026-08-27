cmake_minimum_required(VERSION 3.17)

# 动态库名：产物 = sps_gdextension + 平台后缀，落盘到 godot/project/bin/<platform>/
set(LIBNAME "sps_gdextension")
set(GODOT_PROJECT_DIR "project")

project(${LIBNAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Python3 3.4 REQUIRED)   # godot-cpp 绑定生成器需要
find_program(GIT git REQUIRED)

# godot-cpp 为 git submodule（.gitmodules 记录 url/branch，gitlink 指针，源码不入库）
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/src")
  message(FATAL_ERROR "godot-cpp missing. Init submodule first: "
          "git submodule update --init godot/godot-cpp")
endif()

# 消费端关键：SYSTEM 避免第三方头文件警告轰炸
add_subdirectory(godot-cpp SYSTEM)

# 从 godot::cpp 目标读取生成的后缀/平台名（不同平台产物名不同）
get_target_property(GODOTCPP_SUFFIX godot::cpp GODOTCPP_SUFFIX)
get_target_property(GODOTCPP_PLATFORM godot::cpp GODOTCPP_PLATFORM)

# ---------- core 源码直接编入动态库（复用主工程算法，零 Godot 依赖） ----------
set(SPS_CORE_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/analyzer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/image/image.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/mask/mask.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/mask/alpha_mask.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/mask/mask_io.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/mask/morphology.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/segmentation/background.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/segmentation/background_remover.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/segmentation/auto_detector.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/segmentation/connected_components.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/segmentation/grid_detector.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/segmentation/splitter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/export/png_exporter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/export/json_exporter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/export/sheet.cpp
)

# extra：Remote 背景后端（httplib，与 CLI 同源注册表；register_backend 幂等）
set(SPS_EXTRA_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/../extra/bg_remote/bg_remote.cpp
)

add_library(${LIBNAME} SHARED
  src/register_types.cpp
  src/conversion.cpp
  src/sprite_splitter.cpp
  src/sprite_sheet_builder.cpp
  ${SPS_CORE_SRC}
  ${SPS_EXTRA_SRC}
)

target_link_libraries(${LIBNAME} PRIVATE godot-cpp)

target_include_directories(${LIBNAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../core
  ${CMAKE_CURRENT_SOURCE_DIR}/../extra/bg_remote
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/httplib
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/json
)

# 产物命名与落盘：名 = LIBNAME + 平台后缀，直接放 addons/<插件>/bin/<platform>/
set_target_properties(${LIBNAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/project/addons/sprite_splitter/bin/${GODOTCPP_PLATFORM}>"
  LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/project/addons/sprite_splitter/bin/${GODOTCPP_PLATFORM}>"
  PREFIX "" OUTPUT_NAME "${LIBNAME}${GODOTCPP_SUFFIX}"
)
