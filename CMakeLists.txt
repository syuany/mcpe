# 最低版本要求
cmake_minimum_required(VERSION 3.14)
project(MCPE)

# 使用 C++23（asio 和 spdlog 都推荐现代标准）
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # 生成 compile_commands.json 供 clangd 使用

# 统一二进制产物输出路径：主程序输出至 bin/，归档静态库输出至 build/lib/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 开启编译警告（便于发现潜在问题）
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 添加项目根目录及 src 目录到 Include 搜索路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ==============================================
# 第三方依赖库（本地内嵌 lib/ 目录，零网络依赖）
# ==============================================

# 1. Asio (纯头文件接口库)
add_library(asio INTERFACE)
target_include_directories(asio SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/lib/asio/include)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)

find_package(Threads REQUIRED)
target_link_libraries(asio INTERFACE Threads::Threads)

if(WIN32)
    target_link_libraries(asio INTERFACE ws2_32 wsock32)
    target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0A00)
endif()

# 2. spdlog (纯头文件接口库，零编译开销)
add_library(spdlog INTERFACE)
target_include_directories(spdlog SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/lib/spdlog/include)
target_link_libraries(spdlog INTERFACE Threads::Threads)
add_library(spdlog::spdlog ALIAS spdlog)

# 3. GoogleTest (单元测试框架)
# Windows 上避免 gtest 强制替换 CRT 设置
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(lib/googletest EXCLUDE_FROM_ALL)

# 4. jemalloc 高性能多线程内存分配器（预编译静态库，强制依赖）
set(JEMALLOC_PREBUILT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/jemalloc")

if(EXISTS "${JEMALLOC_PREBUILT_DIR}/lib/libjemalloc.a" AND EXISTS "${JEMALLOC_PREBUILT_DIR}/include/jemalloc/jemalloc.h")
    set(JEMALLOC_LIB "${JEMALLOC_PREBUILT_DIR}/lib/libjemalloc.a")
    set(JEMALLOC_INCLUDE_DIR "${JEMALLOC_PREBUILT_DIR}/include")
    message(STATUS "Using embedded prebuilt jemalloc: ${JEMALLOC_LIB}")
else()
    # 兜底查找系统已安装的 jemalloc
    find_path(JEMALLOC_INCLUDE_DIR
        NAMES jemalloc/jemalloc.h
        PATHS /usr/include /usr/local/include
    )
    find_library(JEMALLOC_LIB
        NAMES jemalloc
        PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib
    )
    if(NOT JEMALLOC_LIB)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(JEMALLOC QUIET jemalloc)
            if(JEMALLOC_FOUND)
                set(JEMALLOC_LIB ${JEMALLOC_LIBRARIES})
                set(JEMALLOC_INCLUDE_DIR ${JEMALLOC_INCLUDE_DIRS})
            endif()
        endif()
    endif()
endif()

if(NOT JEMALLOC_LIB)
    message(FATAL_ERROR 
        "jemalloc is strictly REQUIRED but was not found in lib/jemalloc or system!\n"
        "Please provide prebuilt lib/jemalloc or install development package:\n"
        "  Debian/Ubuntu: sudo apt-get install -y libjemalloc-dev\n"
        "  RHEL/CentOS:   sudo yum install -y jemalloc-devel\n"
        "  macOS:         brew install jemalloc\n"
    )
endif()

message(STATUS "Found jemalloc library: ${JEMALLOC_LIB}")
if(JEMALLOC_INCLUDE_DIR)
    message(STATUS "Found jemalloc include: ${JEMALLOC_INCLUDE_DIR}")
endif()

# 创建全局静态导入目标 jemalloc::jemalloc
add_library(jemalloc::jemalloc STATIC IMPORTED GLOBAL)
set_target_properties(jemalloc::jemalloc PROPERTIES
    IMPORTED_LOCATION "${JEMALLOC_LIB}"
)
if(JEMALLOC_INCLUDE_DIR)
    set_target_properties(jemalloc::jemalloc PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${JEMALLOC_INCLUDE_DIR}"
    )
endif()

# ==============================================
# 模块1: mcpe_common 静态库
# ==============================================
add_library(mcpe_common STATIC
    src/common/ByteBuffer.cc
    src/common/VarInt.cc
)

target_include_directories(mcpe_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_options(mcpe_common PRIVATE -O3)
target_link_libraries(mcpe_common PUBLIC spdlog::spdlog asio jemalloc::jemalloc ${CMAKE_DL_LIBS})

# ==============================================
# 模块2: mcpe_network 静态库
# ==============================================
add_library(mcpe_network STATIC
    src/network/TcpSession.cc
    src/network/TcpServer.cc
    src/network/PacketFraming.cc
)

target_include_directories(mcpe_network PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(mcpe_network PUBLIC mcpe_common)

# ==============================================
# 模块3: mcpe_protocol 静态库
# ==============================================
add_library(mcpe_protocol STATIC
    src/protocol/ClientSession.cc
    src/protocol/PacketRouter.cc
    src/protocol/StatusHandler.cc
    src/protocol/AuthUtils.cc
    src/protocol/LoginHandler.cc
    src/protocol/PlayHandler.cc
)

target_include_directories(mcpe_protocol PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(mcpe_protocol PUBLIC mcpe_network mcpe_common)

# ==============================================
# 可执行文件 server
# ==============================================
add_executable(server src/server.cc)

target_link_libraries(server PRIVATE mcpe_protocol mcpe_network mcpe_common)

# ==============================================
# 单元测试（归档输出至 bin/tests 保持顶层 bin 清洁）
# ==============================================
enable_testing()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/tests)

add_executable(test_bytebuffer
    tests/test_bytebuffer.cc
)

target_link_libraries(test_bytebuffer PRIVATE
    mcpe_common
    GTest::gtest_main
)

# VarInt / VarLong 边界极限测试（TASK-0.5）
add_executable(test_varint
    tests/test_varint.cc
)

target_link_libraries(test_varint PRIVATE
    mcpe_common
    GTest::gtest_main
)

# TcpSession 消息队列与并发安全测试（TASK-1.3）
add_executable(test_tcp_session
    tests/test_tcp_session.cc
)

target_link_libraries(test_tcp_session PRIVATE
    mcpe_network
    mcpe_common
    GTest::gtest_main
)

# PacketFraming 测试（TASK-1.4）
add_executable(test_framing
    tests/test_framing.cc
)

target_link_libraries(test_framing PRIVATE
    mcpe_network
    mcpe_common
    GTest::gtest_main
)

# PacketRouter 测试（TASK-1.5）
add_executable(test_packet_router
    tests/test_packet_router.cc
)

target_link_libraries(test_packet_router PRIVATE
    mcpe_protocol
    mcpe_network
    mcpe_common
    GTest::gtest_main
)

# UUID 测试
add_executable(test_uuid
    tests/test_uuid.cc
)

target_link_libraries(test_uuid PRIVATE
    mcpe_protocol
    GTest::gtest_main
)

# PacketWriter 测试（RAII 发包器）
add_executable(test_packet_writer
    tests/test_packet_writer.cc
)

target_link_libraries(test_packet_writer PRIVATE
    mcpe_network
    mcpe_common
    GTest::gtest_main
)

# 让 CTest 自动发现每个 TEST 用例
include(GoogleTest)
gtest_discover_tests(test_bytebuffer)
gtest_discover_tests(test_varint)
gtest_discover_tests(test_tcp_session)
gtest_discover_tests(test_framing)
gtest_discover_tests(test_packet_router)
gtest_discover_tests(test_uuid)
gtest_discover_tests(test_packet_writer)
