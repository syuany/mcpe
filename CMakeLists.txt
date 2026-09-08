# 最低版本要求（FetchContent 需要 3.14+）
cmake_minimum_required(VERSION 3.14)
project(MCPE)

# 使用 C++17（asio 和 spdlog 都推荐）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启编译警告（便于发现潜在问题）
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ----------------------------------------------
# 引入 FetchContent
# ----------------------------------------------
include(FetchContent)

# ----------------------------------------------
# 导入 spdlog
# ----------------------------------------------
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# ----------------------------------------------
# 导入 asio（纯头文件库）
# ----------------------------------------------
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
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
    src/common/hello.cc
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
    CXX_STANDARD 17
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
enable_testing()

add_executable(hello_test tests/hello_test.cc)

# 链接 mcpe_common（测试需要调用库函数）
target_link_libraries(hello_test PRIVATE mcpe_common)

# 将测试注册到 CTest
add_test(NAME hello_test COMMAND hello_test)

# 也可以给测试目标设置同样的编译标准
set_target_properties(hello_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)