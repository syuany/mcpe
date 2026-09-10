# 最低版本要求（FetchContent 需要 3.14+）
cmake_minimum_required(VERSION 3.14)
project(MCPE)

# 使用 C++17（asio 和 spdlog 都推荐）
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # 生成 compile_commands.json 供 clangd 使用


# 开启编译警告（便于发现潜在问题）
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 添加项目根目录到 Include 搜索路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------------------
# 引入 FetchContent
# ----------------------------------------------
include(FetchContent)

# ----------------------------------------------
# GitHub 加速镜像（国内网络）
# ----------------------------------------------
# 背景：直连 github.com 的 git 协议不稳定（会被 reset），
#       而 FetchContent 的 git clone 没有超时，一旦连不上就会一直卡住。
# 关闭方式：-DMCPE_USE_GITHUB_MIRROR=OFF，或 -DMCPE_GIT_MIRROR=
option(MCPE_USE_GITHUB_MIRROR "通过镜像前缀下载依赖（国内网络推荐）" ON)
set(MCPE_GIT_MIRROR "https://ghproxy.net/" CACHE STRING "GitHub 镜像前缀，留空则直连")

set(MCPE_GIT_PREFIX "")
if(MCPE_USE_GITHUB_MIRROR AND MCPE_GIT_MIRROR)
    set(MCPE_GIT_PREFIX "${MCPE_GIT_MIRROR}")
    message(STATUS "依赖下载使用镜像: ${MCPE_GIT_PREFIX}")
else()
    message(STATUS "依赖下载直连 GitHub")
endif()

# ----------------------------------------------
# 导入 spdlog
# ----------------------------------------------
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY ${MCPE_GIT_PREFIX}https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# ----------------------------------------------
# 导入 asio（纯头文件库）
# ----------------------------------------------
FetchContent_Declare(
    asio
    GIT_REPOSITORY ${MCPE_GIT_PREFIX}https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    GIT_SHALLOW    TRUE
)

FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)

    add_library(asio INTERFACE)
    target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE)

    find_package(Threads REQUIRED)
    target_link_libraries(asio INTERFACE Threads::Threads)

    if(WIN32)
        target_link_libraries(asio INTERFACE ws2_32 wsock32)
        target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0A00)
    endif()
endif()

# ----------------------------------------------
# 模块1：mcpe_common 静态库
# ----------------------------------------------
# 【显式列出源文件，不使用 GLOB】
add_library(mcpe_common STATIC
    src/common/ByteBuffer.cc
)

# 导出头文件目录（PUBLIC 使链接者也能获得该路径）
target_include_directories(mcpe_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 链接公共库所需的依赖（这里需要 spdlog 和 asio，因为 hello.cc 用了 spdlog）
target_link_libraries(mcpe_common PUBLIC spdlog::spdlog asio)

# 可选：为库目标设置 C++ 标准（继承自项目全局）
set_target_properties(mcpe_common PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

# ----------------------------------------------
# 可执行文件 main_app
# ----------------------------------------------
add_executable(main src/main.cc)

# 链接 mcpe_common（私有链接即可，因为 main 不需要暴露内部依赖）
target_link_libraries(main PRIVATE mcpe_common)

# ----------------------------------------------
# 单元测试（仅在开启测试时构建）
# ----------------------------------------------

# ----------------------------------------------
# 引入 GoogleTest 并构建单元测试
# ----------------------------------------------
FetchContent_Declare(
    googletest
    GIT_REPOSITORY ${MCPE_GIT_PREFIX}https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)

# # Windows 上避免 gtest 强制替换 CRT 设置
# set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(test_bytebuffer
    tests/test_bytebuffer.cc
)

target_link_libraries(test_bytebuffer PRIVATE
    mcpe_common
    GTest::gtest_main
)

set_target_properties(test_bytebuffer PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

# 让 CTest 自动发现每个 TEST 用例
include(GoogleTest)
gtest_discover_tests(test_bytebuffer)