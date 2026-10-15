cmake_minimum_required(VERSION 3.14)
project(MCPE LANGUAGES CXX)

# 使用 C++23 标准并生成 compile_commands.json 供 clangd 使用
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 统一二进制产物输出路径：主程序与离线工具输出至 bin/，静态库输出至 build/lib/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 开启严格编译警告门禁
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 引入第三方依赖管理模块（Asio / spdlog / GoogleTest / Zlib）
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

# 按职责分层构建：运行时核心 (src) -> 离线工具链 (tools) -> 单元测试矩阵 (tests)
enable_testing()
add_subdirectory(src)
add_subdirectory(tools)
add_subdirectory(tests)
