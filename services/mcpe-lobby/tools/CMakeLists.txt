# ==============================================================================
# tools/CMakeLists.txt - 离线地图解析与烘焙工具链 (链接 ZLIB)
# ==============================================================================

add_library(mcpe_map_io STATIC
    map/ZipArchive.cc
    map/SchematicLoader.cc
    map/AnvilLoader.cc
)
target_include_directories(mcpe_map_io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/map>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
target_link_libraries(mcpe_map_io PUBLIC mcpe_world mcpe_common ZLIB::ZLIB)

# 离线地图烘焙器命令行工具 (bin/map_baker)
add_executable(map_baker map/main.cc)
target_link_libraries(map_baker PRIVATE mcpe_map_io mcpe_world mcpe_common ZLIB::ZLIB)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(mcpe_map_io PRIVATE -Wno-dangling-reference)
    target_compile_options(map_baker PRIVATE -Wno-dangling-reference)
endif()
