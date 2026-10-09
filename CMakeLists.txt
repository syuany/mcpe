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
# 第三方依赖库（基于 CMake FetchContent 管理）
# ==============================================
include(FetchContent)

# 1. Asio (纯头文件接口库，版本 1.30.2)
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

# 2. spdlog (高效日志库，v1.13.0)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# 3. GoogleTest (单元测试框架，v1.14.0)
# Windows 上避免 gtest 强制替换 CRT 设置
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)

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
target_link_libraries(mcpe_common PUBLIC spdlog::spdlog asio)

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
# 模块3: mcpe_world 静态库
# ==============================================
add_library(mcpe_world STATIC
    src/world/ChunkSection.cc
    src/world/Chunk.cc
    src/world/FlatWorldGenerator.cc
)

target_include_directories(mcpe_world PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(mcpe_world PUBLIC mcpe_common)

# ==============================================
# 模块4: mcpe_protocol 静态库
# ==============================================
add_library(mcpe_protocol STATIC
    src/protocol/ClientSession.cc
    src/protocol/ClientManager.cc
    src/protocol/HandshakeHandler.cc
    src/protocol/PacketRouter.cc
    src/protocol/StatusHandler.cc
    src/protocol/AuthUtils.cc
    src/protocol/LoginHandler.cc
    src/protocol/PlayHandler.cc
    src/protocol/ChunkSerializer.cc
)

target_include_directories(mcpe_protocol PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(mcpe_protocol PUBLIC mcpe_world mcpe_network mcpe_common)

# ==============================================
# 可执行文件 server
# ==============================================
add_executable(server src/main.cc)

target_link_libraries(server PRIVATE mcpe_protocol mcpe_world mcpe_network mcpe_common)

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

# Chunk 世界与序列化测试（TASK-3.6）
add_executable(test_chunk
    tests/test_chunk.cc
)

target_link_libraries(test_chunk PRIVATE
    mcpe_protocol
    mcpe_world
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
gtest_discover_tests(test_chunk)

# EventQueue 并发队列与 std::variant 事件测试（TASK-4.1）
add_executable(test_event_queue
    tests/test_event_queue.cc
)

target_link_libraries(test_event_queue PRIVATE
    mcpe_world
    mcpe_common
    GTest::gtest_main
)

gtest_discover_tests(test_event_queue)