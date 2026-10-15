# ==============================================================================
# tests/CMakeLists.txt - GoogleTest 自动化单元测试矩阵（封装辅助函数消除重复）
# ==============================================================================
include(GoogleTest)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests)

function(mcpe_add_test TEST_NAME SOURCE_FILE)
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    target_link_libraries(${TEST_NAME} PRIVATE ${ARGN} GTest::gtest_main)
    gtest_discover_tests(${TEST_NAME})
endfunction()

# 1. Foundations 基础层测试
mcpe_add_test(test_bytebuffer    test_bytebuffer.cc    mcpe_common)
mcpe_add_test(test_varint        test_varint.cc        mcpe_common)

# 2. Network 网络层测试
mcpe_add_test(test_tcp_session   test_tcp_session.cc   mcpe_network mcpe_common)
mcpe_add_test(test_framing       test_framing.cc       mcpe_network mcpe_common)
mcpe_add_test(test_packet_writer test_packet_writer.cc mcpe_network mcpe_common)

# 3. World 领域与空间 AOI 层测试
mcpe_add_test(test_chunk         test_chunk.cc         mcpe_protocol mcpe_world mcpe_network mcpe_common)
mcpe_add_test(test_event_queue   test_event_queue.cc   mcpe_world mcpe_common)
mcpe_add_test(test_world         test_world.cc         mcpe_world mcpe_common)
mcpe_add_test(test_spatial_grid  test_spatial_grid.cc  mcpe_world mcpe_common)

# 4. Protocol 协议与多玩家联机测试
mcpe_add_test(test_uuid          test_uuid.cc          mcpe_protocol)
mcpe_add_test(test_packet_router test_packet_router.cc mcpe_protocol mcpe_world mcpe_network mcpe_common)
mcpe_add_test(test_sprint3       test_sprint3.cc       mcpe_protocol mcpe_world mcpe_network mcpe_common)

# 5. Tools 离线地图解析与归档测试
mcpe_add_test(test_zip_archive   test_zip_archive.cc   mcpe_map_io mcpe_common ZLIB::ZLIB)
mcpe_add_test(test_schematic     test_schematic.cc     mcpe_map_io mcpe_world mcpe_common ZLIB::ZLIB)
mcpe_add_test(test_anvil_loader  test_anvil_loader.cc  mcpe_map_io mcpe_world mcpe_common ZLIB::ZLIB)
