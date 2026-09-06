# 最低版本要求（FetchContent 需要 3.14+）
cmake_minimum_required(VERSION 3.14)
project(MCPE)

# 使用 C++17（asio 和 spdlog 都推荐）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------
# 模块1：引入 FetchContent（CMake 内置的“在线下载”功能）
# ----------------------------------------------
include(FetchContent)

# ----------------------------------------------
# 导入 spdlog（它官方提供了完善的 CMake 支持）
# ----------------------------------------------
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0          # 稳定版本号
    GIT_SHALLOW    TRUE             # 浅克隆，避免下载全部历史 commit 导致卡住
)
FetchContent_MakeAvailable(spdlog)
# 上面这行执行完后，会生成一个叫 spdlog::spdlog 的 CMake 目标（Target）

# ----------------------------------------------
# 导入 asio（纯头文件库，需手动封装 INTERFACE 库）
# ----------------------------------------------
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2      # 稳定版本号
    GIT_SHALLOW    TRUE             # 浅克隆，大幅缩短拉取时间
)

# 因为 asio 没有 CMakeLists.txt，不能直接用 MakeAvailable，要手动 Pull
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
    
    # 手动创建一个“接口库”（Interface Library，专门给纯头文件库用的）
    add_library(asio INTERFACE)
    
    # 告诉编译器去 asio 的 include 文件夹找头文件（注意：asio 源码在其仓库的 asio/ 子目录下）
    target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
    
    # 【关键】定义 ASIO_STANDALONE，告诉 asio “我们不使用 Boost，只用标准库”
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE)

    # 引入系统线程库（Linux/Unix 下 Asio 底层必须依赖 pthread）
    find_package(Threads REQUIRED)
    target_link_libraries(asio INTERFACE Threads::Threads)
    
    # Windows 平台网络库兼容
    if(WIN32)
        target_link_libraries(asio INTERFACE ws2_32 wsock32)
        target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0A00)
    endif()
endif()

# ----------------------------------------------
# 创建我们的可执行文件
# ----------------------------------------------
add_executable(main_app src/main.cc)

# 【核心操作】把两个库链接进来
# 因为 asio 是 INTERFACE 库，它自带的 include 路径和宏定义会自动传给 main_app
# spdlog 同理
target_link_libraries(main_app PRIVATE spdlog::spdlog asio)