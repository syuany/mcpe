# 最低版本要求
cmake_minimum_required(VERSION 3.14)
project(MCPE)

# 使用 C++23（asio 和 spdlog 都推荐现代标准）
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # 生成 compile_commands.json 供 clangd 使用

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

# 2. spdlog (静态库)
add_subdirectory(lib/spdlog EXCLUDE_FROM_ALL)

# 3. GoogleTest (单元测试框架)
# Windows 上避免 gtest 强制替换 CRT 设置
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(lib/googletest EXCLUDE_FROM_ALL)

# ==============================================
# 模块1: mcpe_common 静态库
# ==============================================
add_library(mcpe_common STATIC
    src/common/ByteBuffer.cc
    src/common/VarInt.cc
    src/common/BufferPool.cc
    src/common/ShardedBufferPool.cc
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
# 模块3: mcpe_protocol 静态库
# ==============================================
add_library(mcpe_protocol STATIC
    src/protocol/PacketRouter.cc
    src/protocol/StatusHandler.cc
    src/protocol/AuthUtils.cc
    src/protocol/LoginHandler.cc
)

target_include_directories(mcpe_protocol PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(mcpe_protocol PUBLIC mcpe_network mcpe_common)

# ==============================================
# 可执行文件 main_app
# ==============================================
add_executable(main src/main.cc)

target_link_libraries(main PRIVATE mcpe_protocol mcpe_network mcpe_common)

# ==============================================
# 单元测试
# ==============================================
enable_testing()

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

# BufferPool 测试
add_executable(test_buffer_pool
    tests/test_buffer_pool.cc
)

target_compile_options(test_buffer_pool PRIVATE -O3)
target_link_libraries(test_buffer_pool PRIVATE
    mcpe_common
    GTest::gtest_main
)

# ShardedBufferPool 分片内存池与工作窃取测试
add_executable(test_sharded_buffer_pool
    tests/test_sharded_buffer_pool.cc
)

target_compile_options(test_sharded_buffer_pool PRIVATE -O3)
target_link_libraries(test_sharded_buffer_pool PRIVATE
    mcpe_common
    GTest::gtest_main
)

# SessionReadBuffer 零常驻池化网络接收缓冲测试
add_executable(test_session_read_buffer
    tests/test_session_read_buffer.cc
)

target_compile_options(test_session_read_buffer PRIVATE -O3)
target_link_libraries(test_session_read_buffer PRIVATE
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
gtest_discover_tests(test_buffer_pool)
gtest_discover_tests(test_sharded_buffer_pool)
gtest_discover_tests(test_session_read_buffer)

