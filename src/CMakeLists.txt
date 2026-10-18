# ==============================================================================
# src/CMakeLists.txt - 运行时核心五层静态库与 server 主程序
# ==============================================================================

# 1. Foundations 底层基础库 (零业务依赖，零 Zlib 依赖)
add_library(mcpe_common STATIC
    common/ByteBuffer.cc
    common/VarInt.cc
    common/IniConfig.cc
)
target_include_directories(mcpe_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_compile_options(mcpe_common PRIVATE -O3)
target_link_libraries(mcpe_common PUBLIC spdlog::spdlog asio)

# 2. Network 异步网络传输层
add_library(mcpe_network STATIC
    network/TcpSession.cc
    network/TcpServer.cc
    network/PacketFraming.cc
)
target_include_directories(mcpe_network PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(mcpe_network PUBLIC mcpe_common)

# 3. World 游戏世界与空间 AOI 领域底座层 (纯运行时，零 Zlib 依赖)
add_library(mcpe_world STATIC
    world/ChunkSection.cc
    world/Chunk.cc
    world/FlatWorldGenerator.cc
    world/EntityManager.cc
    world/WorldTicker.cc
    world/World.cc
    world/SpatialGrid.cc
)
target_include_directories(mcpe_world PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(mcpe_world PUBLIC mcpe_common)

# 4. Game 多实例调度与大厅分线层 (LobbySharding)
add_library(mcpe_game STATIC
    game/IGameInstance.cc
    game/InstanceManager.cc
    game/lobby/LobbyInstance.cc
)
target_include_directories(mcpe_game PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(mcpe_game PUBLIC mcpe_world mcpe_network mcpe_common)

# 5. Protocol 协议状态机与会话路由层
add_library(mcpe_protocol STATIC
    protocol/ClientSession.cc
    protocol/ClientManager.cc
    protocol/HandshakeHandler.cc
    protocol/PacketRouter.cc
    protocol/StatusHandler.cc
    protocol/AuthUtils.cc
    protocol/LoginHandler.cc
    protocol/PlayHandler.cc
    protocol/ChunkSerializer.cc
    protocol/ChatHandler.cc
)
target_include_directories(mcpe_protocol PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(mcpe_protocol PUBLIC mcpe_game mcpe_world mcpe_network mcpe_common)

# 6. 线上服务端主程序 (bin/server)
add_executable(server main.cc)
target_link_libraries(server PRIVATE mcpe_protocol mcpe_game mcpe_world mcpe_network mcpe_common)
