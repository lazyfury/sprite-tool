---
name: godot-gdextension
description: Godot 4.x C++ 扩展（GDExtension / godot-cpp）制作指南。从零搭建 GDExtension 插件（类注册、属性/信号绑定、.gdextension 配置、CMake 或 SCons 构建、编辑器加载验证），以及 godot::Image ↔ 自有 C++ 数据结构的数据转换。需要为 Godot 写 C++ 插件、把现有 C++ 库封装成 Godot 可调用的类、或用 godot-cpp 做高性能脚本时使用。
---

# Godot 4.x GDExtension (godot-cpp) 制作指南

> 版本锚点：Godot **4.6.2.stable**（本机 `/Applications/Godot.app`）+ godot-cpp **master (v10.x beta)**，2026-08 实测。
> 官方文档的 C++ 章节自 4.6 起迁移到 `tutorials/scripting/cpp/`（旧的 `gdextension/` 路径已 404）。

## 0. 一句话流程

```
克隆 godot-cpp（submodule）→ src/ 写类（GDCLASS）→ register_types 注册 → 编译动态库 → 写 .gdextension → Godot 加载验证
```

## 1. 版本与兼容性（决定用哪个分支）

| 项 | 事实 |
|---|---|
| godot-cpp master | **v10.x，beta**，独立于 Godot 版本号；要求显式 `api_version`，支持 Godot 4.3+（含 4.6） |
| godot-cpp 稳定分支 | `godot-4.5-stable` / `4.5`（旧绑定体系，不带 api_version 机制） |
| 兼容规则 | **低版本扩展可在高版本 Godot 运行，反之不行**。例：target 4.5 的扩展可在 4.6 跑；target 4.6 的不能跑在 4.5。4.0 是例外（4.1+ 全不兼容） |
| 建议 | 目标 **最低**满足需求的 Godot 版本，别追最新（省去多版本构建） |
| 浮点精度 | 扩展必须与引擎同精度：官方构建=single；double 引擎必须用其 `extension_api.json` 重编扩展 |
| 自定义引擎 | 自编译 Godot 要用 `godot --dump-extension-api` 生成的 api 文件构建扩展 |

2026-08 实测结论（sprite-tool 场景）：本机仅 4.6.2 → 用 **master v10.x + `api_version=4.6`**（精确匹配 4.6 API）；或 `godot-4.5-stable` 构建（兼容 4.6）。v10 是 beta，正式发布选型再评估。

## 2. 项目结构（官方模板 layout）

### 2.1 纯 GDExtension（无编辑器 UI）——官方模板

```
gdextension_cpp_example/
├── project/            # Godot 测试工程（含 main.tscn）
│   └── bin/            # 动态库 + .gdextension 放这里
├── godot-cpp/          # submodule 或 vendored
└── src/
    ├── register_types.h / register_types.cpp
    ├── my_class.h / my_class.cpp     # 每个可实例化类一对文件
    └── SConstruct 或 CMakeLists.txt  # 构建脚本
```

### 2.2 带编辑器 UI（EditorPlugin）——addons 规范布局（官方插件标准）

```
project/
├── addons/
│   └── sprite_splitter/                # 插件目录（名 = 插件名）
│       ├── plugin.cfg                  # EditorPlugin 注册（[plugin] name/description/author/version/script）
│       ├── editor_plugin.gd            # @tool + extends EditorPlugin（script 字段指向）
│       ├── bin/
│       │   ├── sps_gdextension.gdextension   # GDExtension 配置（放 addons 下一样被扫描）
│       │   └── macos/xxx.arm64.dylib         # 动态库（gitignore 掉，.gdextension 保留提交）
│       └── (icon.svg 等资源)
├── project.godot
└── 测试脚本/素材
```

- `plugin.cfg` 是编辑器插件唯一标识：存在有效 `[plugin]` + `script` 指向 `@tool extends EditorPlugin` 脚本，才会出现在 **Project > Project Settings > Plugins** 列表（默认未启用，手动勾选或 GDScript 里 `EditorInterface.set_plugin_enabled`）
- 插件用到的**所有 GDScript 都要 `@tool`**，否则编辑器里像空文件
- 扩展类（C++）可直接在 editor_plugin.gd 里 `SpriteSplitter.new()` 使用——GDExtension 类对 GDScript 全局可见
- 官方模板：`https://github.com/godotengine/godot-cpp-template`（带 CI workflow）
- 获取 godot-cpp：`git submodule add -b <branch> https://github.com/godotengine/godot-cpp` 或直接 clone
- 模板 CMakeLists.txt 在模板仓库根目录，是消费端最佳实践参考

## 2.5 GDExtension 加载机制（4.6 源码实测）

```
编辑器启动（全项目递归扫描 *.gdextension，含 addons/）
        │  把路径追加写入 res://.godot/extension_list.cfg（一行一个）
        ▼
运行/导出时 GDExtensionManager::load_extensions()
        │  逐行读 extension_list.cfg → load_extension(path) → dlsym(entry_symbol) → 初始化
        ▼
类注册（GDREGISTER_CLASS）→ GDScript/编辑器可见
```

- **发现机制**：运行时**不扫描** res://，只读 `.godot/extension_list.cfg`（`ProjectSettings.get_project_data_path().path_join("extension_list.cfg")`）；该列表由编辑器维护（EditorFileSystem 扫描 .gdextension 时写入）
- 推论：全新项目/CI 无编辑器时，需先跑一次编辑器模式生成列表（或手动写 .godot/extension_list.cfg）；`.gdextension` 放 addons/ 下与放 bin/ 无差别，都会被扫描
- 加载时机：`Main::setup` → `GDExtensionManager::load_extensions()`（引擎早期，类注册先于脚本）；编辑器热重载走 `reload_extensions()`（仅 reloadable=true）
- `[libraries]` 按 平台.debug|release[.arch] 匹配；导出时只打包匹配平台的那一个库
- 运行时按需加载 `[dependencies]`（第三方动态库）

## 3. 核心代码模板

### 3.1 类头文件 `src/my_class.h`

```cpp
#pragma once
#include <godot_cpp/classes/sprite2d.hpp>   // 想继承什么就 include 什么

namespace godot {

class MyClass : public Sprite2D {
    GDCLASS(MyClass, Sprite2D)              // 必需宏：注入反射元数据

private:
    double amplitude;
    double speed;

protected:
    static void _bind_methods();            // 声明要暴露给 GDScript 的方法/属性/信号

public:
    MyClass();
    ~MyClass();

    void _process(double delta) override;   // 与 GDScript 同名虚函数
    void set_amplitude(const double p_amplitude);
    double get_amplitude() const;
    void set_speed(const double p_speed);
    double get_speed() const;
};

} // namespace godot
```

### 3.2 实现 `src/my_class.cpp`

```cpp
#include "my_class.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void MyClass::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_amplitude"), &MyClass::get_amplitude);
    ClassDB::bind_method(D_METHOD("set_amplitude", "p_amplitude"), &MyClass::set_amplitude);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amplitude"), "set_amplitude", "get_amplitude");

    // 带范围提示的属性：min,max,step
    ClassDB::bind_method(D_METHOD("get_speed"), &MyClass::get_speed);
    ClassDB::bind_method(D_METHOD("set_speed", "p_speed"), &MyClass::set_speed);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed", PROPERTY_HINT_RANGE, "0,20,0.01"), "set_speed", "get_speed");

    // 信号：MethodInfo(信号名, 参数 PropertyInfo...)
    ADD_SIGNAL(MethodInfo("position_changed", PropertyInfo(Variant::OBJECT, "node"), PropertyInfo(Variant::VECTOR2, "new_pos")));
}

// 构造/析构/实现略……

// 发射信号
emit_signal("position_changed", this, new_position);

// 连接别的对象信号（方法必须先注册）
some_other_node->connect("the_signal", Callable(this, "my_method"));
```

要点：
- 所有要暴露的东西（方法/属性/信号）**必须**在 `_bind_methods()` 注册，否则 GDScript 侧不存在
- `D_METHOD("name", "arg1", "arg2")` 第二个参数起是参数名（文档用）
- `ADD_PROPERTY(PropertyInfo, setter, getter)` 的 setter/getter 必须已 bind

### 3.3 注册入口 `src/register_types.cpp` + `register_types.h`

```cpp
// register_types.h
#pragma once
#include <godot_cpp/core/class_db.hpp>
using namespace godot;
void initialize_example_module(ModuleInitializationLevel p_level);
void uninitialize_example_module(ModuleInitializationLevel p_level);
```

```cpp
// register_types.cpp
#include "register_types.h"
#include "my_class.h"
#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_example_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(MyClass);              // 每类一个
}

void uninitialize_example_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
}

extern "C" {
GDExtensionBool GDE_EXPORT example_library_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        const GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_example_module);
    init_obj.register_terminator(uninitialize_example_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}
```

- **`example_library_init` 这个名字必须与 `.gdextension` 的 `entry_symbol` 一致**
- 初始化级别：`CORE` / `SERVERS` / `SCENE` / `EDITOR` / `LEVEL`；大多数插件用 SCENE

## 4. `.gdextension` 文件（放 `project/bin/`，与动态库同目录）

```ini
[configuration]
entry_symbol = "example_library_init"
compatibility_minimum = "4.1"      # 低于此版本的 Godot 拒绝加载
reloadable = true                   # 编辑器热重载，仅 debug 构建生效
                                    # ⚠️ 4.6.2 下 =true 会让编辑器模式崩溃（见 §7），需编辑器时改 false

[libraries]
macos.debug = "./libmyext.macos.template_debug.dylib"
macos.release = "./libmyext.macos.template_release.dylib"
windows.debug.x86_64 = "./myext.windows.template_debug.x86_64.dll"
windows.release.x86_64 = "./myext.windows.template_release.x86_64.dll"
linux.debug.x86_64 = "./libmyext.linux.template_debug.x86_64.so"
linux.release.x86_64 = "./libmyext.linux.template_release.x86_64.so"
```

- `[libraries]` 键格式：`平台.debug|release[.架构]`；导出时 Godot 只打包匹配平台的库
- macOS 键无架构段；Linux/Windows 需带架构
- ⚠️ **macOS 产物文件名带架构后缀**（CMake 行为）：`libmyext.macos.template_debug.arm64.dylib`，`.gdextension` 路径必须写 `.arm64`，否则加载失败
- 也可用绝对 res:// 路径：`"res://bin/libxxx.dylib"`

## 5. 构建

### 5.1 CMake（推荐，本机无 scons 时首选）

本机：cmake + ninja ✅、python3 ✅、**scons 未装** → 用 CMake。

选项（注意前缀是 `GODOTCPP_` 不是 `GODOT_CPP_`）：

| 选项 | 默认 | 说明 |
|---|---|---|
| `GODOTCPP_TARGET` | `template_debug` | `template_debug` / `template_release` / `editor`（Godot 的 debug 特性开关，与 CMAKE_BUILD_TYPE 正交） |
| `GODOTCPP_API_VERSION` | (空) | 目标 API 版本，如 `4.6`；会找 `gdextension/extension_api-4-6.json` |
| `GODOTCPP_CUSTOM_API_FILE` | (空) | 自定义 api json（优先级最高；自定义引擎/未来版本用） |
| `GODOTCPP_PRECISION` | `single` | `single` / `double`（必须与引擎一致） |
| `GODOTCPP_DISABLE_EXCEPTIONS` | **ON** | ⚠️ 默认 ON 且以 **PUBLIC** 传播 `-fno-exceptions` 到所有消费者目标！core 用异常的工程必须显式 `-DGODOTCPP_DISABLE_EXCEPTIONS=OFF`，否则消费者编译报 "cannot use 'throw' with exceptions disabled" |
| `GODOTCPP_USE_HOT_RELOAD` | 空 | ON 启用热重载记录 |
| `GODOTCPP_BINDING_HOOK_FILE` | (空) | 绑定生成钩子 py 文件（hook 类名须为 `CustomBindingGeneratorHooks`） |
| `GODOTCPP_GDEXTENSION_DIR` | `gdextension` | 自定义接口头/API json 目录 |

消费端 CMakeLists.txt（官方模板结构）：

```cmake
cmake_minimum_required(VERSION 3.17)
set(LIBNAME "myext")                                # 库名
set(GODOT_PROJECT_DIR "project")
find_package(Python3 3.4 REQUIRED)
find_program(GIT git REQUIRED)

# godot-cpp submodule 缺失时自动初始化
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/godot-cpp/src")
    execute_process(COMMAND git submodule update --init godot-cpp
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} COMMAND_ERROR_IS_FATAL ANY)
endif()

add_subdirectory(godot-cpp SYSTEM)                  # 关键：SYSTEM 避免警告轰炸

# 从 godot::cpp 目标读取生成的后缀/平台名（不同平台产物名不同）
get_target_property(GODOTCPP_SUFFIX godot::cpp GODOTCPP_SUFFIX)
get_target_property(GODOTCPP_PLATFORM godot::cpp GODOTCPP_PLATFORM)

project(${LIBNAME} LANGUAGES CXX)

add_library(${LIBNAME} SHARED
    src/register_types.cpp src/my_class.cpp ...)

target_link_libraries(${LIBNAME} PRIVATE godot-cpp)  # 别名目标 godot::cpp 亦可
set_property(TARGET ${LIBNAME} PROPERTY CXX_STANDARD 17)   # godot-cpp 要求 C++17+

# 产物命名与落盘：名 = LIBNAME + 平台后缀，直接放 project/bin/<platform>
set_target_properties(${LIBNAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${PROJECT_SOURCE_DIR}/bin/${GODOTCPP_PLATFORM}>"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${PROJECT_SOURCE_DIR}/bin/${GODOTCPP_PLATFORM}>"
    PREFIX "" OUTPUT_NAME "${LIBNAME}${GODOTCPP_SUFFIX}"
)
```

构建命令（macOS）：

```bash
cmake -S . -B build-gdext -G Ninja \
    -DGODOTCPP_TARGET=template_debug \
    -DGODOTCPP_API_VERSION=4.6 \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build build-gdext
# 产物：project/bin/<platform>/libmyext.macos.template_debug.dylib
```

- CMake `Debug/Release`（编译符号/优化）与 Godot `template_debug/template_release`（debug 特性宏）是**两个正交概念**；编辑器热重载只认后者
- API 文件选择优先级：`CUSTOM_API_FILE` > `API_VERSION` > `GDEXTENSION_DIR` 默认文件
- 想用本机 4.6 的精确 API：`/Applications/Godot.app/Contents/MacOS/Godot --dump-extension-api` 生成 json 后走 `GODOTCPP_CUSTOM_API_FILE`

### 5.2 SCons（官方主构建系统）

```bash
pip install scons        # 或 brew install scons
scons platform=macos target=template_debug
# 产物默认在 project/bin/
# 常用：platform=macos|windows|linux|android|web，target=template_debug|template_release|editor
# v10 需 api_version：scons api_version=4.6
```

## 6. 数据转换（自有 C++ 库 ↔ Godot）

- **所有通过 GDExtension 暴露的函数必须 Variant 兼容**（基础类型、Vector2/3、Rect2i、Packed*Array、String、Array、Dictionary、Object 指针）
- `Packed*Array` 数据在 Godot 侧内存，**必须用 `.ptr()`（读）/ `.ptrw()`（写）** 直接访问，逐元素下标每次都要跨边界调用：

```cpp
// 错误：for 循环里 p_array[i] 每次调 Godot
// 正确：const uint8_t *p = p_array.ptr(); p[i] 直接读内存
```

- 数组返回给 GDScript：`Array` + `Variant` 包装（`arr.append(rect)`）；或 `PackedVector2Array` 等强类型数组
- godot::Image 像素数据：`image->get_image()->get_data()`（PackedByteArray）→ `.ptrw()` 后按 `Format` 布局（RGBA8 = 每像素 4 字节）读写
- godot-cpp 的 `Vector`（内存在本侧）与 `Packed*Array`（内存在对侧）不要混淆；STL 类型可用但不进 Godot API
- 注意：GDScript 经 Variant 调用时参数可能被拷贝，函数内修改不一定回写到调用方

## 7. 验证工作流（本机）

```bash
# 1) 构建（见 §5.1）
# 2) 无头冒烟：直接运行测试工程（main.gd 打印断言 + 退出码）
/Applications/Godot.app/Contents/MacOS/Godot --headless --path godot/project; echo $?
# 3) 场景里加 MyClass 节点 → 属性面板可见 → 运行验证逻辑
# 4) 改代码 → 重编 → 编辑器热重载（需 reloadable=true + template_debug）
```

⚠️ **Godot 4.6.2 编辑器模式崩溃（EditorHelp 扩展文档生成 bug，2026-08 源码+实测定位）**：
- 症状：带 GDExtension 的项目，编辑器模式（`--import` / `-e --quit`）在**退出清理阶段**崩，栈 `EditorHelp::_gen_extensions_docs` ← `DocTools::generate` ← `CallQueue::flush` ← `Main::cleanup`（段错误）
- 根因：`doc->generate()` 遍历扩展类生成文档时，`Main::cleanup` 已部分析构子系统 → 访问失效对象。**与 reloadable 无关**（`--doctool --gdextension-docs` 正常时序不崩；无扩展对照不崩）
- 规避（首次导入两步法）：
  ```bash
  # 1) 临时移走 .gdextension，无扩展导入生成全部缓存（EXIT 0）
  mv addons/xxx/bin/xxx.gdextension /tmp/bak
  godot --headless --path project --import
  # 2) 恢复 .gdextension，手动把路径写进扩展列表（编辑器下次扫描会保持它）
  mv /tmp/bak addons/xxx/bin/xxx.gdextension
  printf 'res://addons/xxx/bin/xxx.gdextension\n' > project/.godot/extension_list.cfg
  # 3) 之后运行/验证走 --headless 运行模式（不触发 EditorHelp，正常）
  ```
- GUI 编辑器打开项目：待实测（headless 必崩）；若 GUI 也崩 → 降级 4.6.1/4.5.x 或等修复版

## 8. 常见陷阱

1. **入口符号不匹配**：`.gdextension` 的 `entry_symbol` ≠ `example_library_init` 的导出名 → 编辑器报 "Library is not a valid GDExtension"
2. **分支选错**：master 没设 api_version 会构建失败/API 错位；`4.x` 分支给 Godot 4.x 用
3. **精度不一致**：double 引擎 + single 扩展 → 崩溃/静默错误；官方构建都是 single
4. **忘了 `GDREGISTER_CLASS`**：类不在创建对话框出现（仅 register_types 里注册过的类可用）
5. **属性没 bind 就 ADD_PROPERTY**：setter/getter 必须先 `ClassDB::bind_method`
6. **C++ 标准**：godot-cpp 需要 C++17+（模板里写 17，想用 C++20 特性自己设）
7. **热重载不生效**：必须是 `template_debug`/editor 构建（Godot 的 debug 特性），且 `.gdextension` 里 `reloadable = true`
8. **macOS 动态库签名/路径**：`.gdextension` 里路径写错、或库不在 res:// 下 → 加载失败；`libraries` 键 `macos.debug` 不带架构
9. **官方文档路径**：4.6 起 C++ 章节在 `tutorials/scripting/cpp/`；rst 源在 `github.com/godotengine/godot-docs` master 分支（raw 直链可读，docs.godotengine.org 页面有反爬）
10. **网络**：godot-cpp 用 **git submodule** 引入（gitlink 指针 + `.gitmodules`，源码不入库，参考 limboai）。本环境 `github.com` git clone 不通（代理 502/Empty reply）→ 初始化走 **api.github.com tarball** 解压到 `godot/godot-cpp` + `git update-index --add --cacheinfo 160000,<full-sha>,godot/godot-cpp` 建立指针（sha 用 `GET api.github.com/repos/godotengine/godot-cpp/commits/<短sha>` 取全量）；其他环境 `git clone --recursive` 或 `git submodule update --init` 即可
11. **`-fno-exceptions` 传染**（2026-08 实测）：`GODOTCPP_DISABLE_EXCEPTIONS` 默认 ON，`target_compile_options(godot-cpp PUBLIC ...)` 会把 `-fno-exceptions` 传播给所有链接它的目标；core 用 `throw` 的工程必须 `-DGODOTCPP_DISABLE_EXCEPTIONS=OFF`
12. **macOS 产物带架构后缀**（2026-08 实测）：CMake 输出 `libxxx.macos.template_debug.arm64.dylib`，`.gdextension` 的 `macos.debug` 路径必须含 `.arm64`
13. **4.6.2 编辑器模式崩溃**（2026-08 实测，见 §7）：带 GDExtension 的项目 `--import`/`-e --quit` 退出时 `EditorHelp::_gen_extensions_docs` 段错误；与 reloadable 无关，用 §7 两步法导入 + 运行模式验证
14. **运行模式不扫描 res://**（4.6 源码实测）：扩展加载完全依赖 `.godot/extension_list.cfg`（编辑器生成）；删 .godot 后直接运行会加载失败/异常，必须先走一次编辑器导入或手动写列表

## 9. 参考链接

- 官方 C++ 入门：`https://docs.godotengine.org/en/latest/tutorials/scripting/cpp/gdextension_cpp_example.html`
- CMake 构建：`.../tutorials/scripting/cpp/build_system/cmake.html`（SCons 在 `scons.html`）
- godot-cpp 仓库：`https://github.com/godotengine/godot-cpp`（cmake/godotcpp.cmake 是选项权威来源）
- 官方模板：`https://github.com/godotengine/godot-cpp-template`
- 本机 Godot：`/Applications/Godot.app/Contents/MacOS/Godot --version` → 4.6.2.stable
