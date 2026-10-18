/**
 * @file test_zip_archive.cc
 * @brief ZipArchive 基础归档解析器单元测试
 */

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstring>
#include <filesystem>
#include <vector>

#include "ZipArchive.h"

using namespace mcpe::common;

namespace {

// 辅助函数：构造一个包含一个 Stored 文件的内存 ZIP 包
std::vector<uint8_t> createInMemoryStoredZip(
    const std::string& filename,
    const std::string& content
) {
  std::vector<uint8_t> zip;

  const uint32_t local_offset = static_cast<uint32_t>(zip.size());
  // 1. Local File Header (30 字节 + name + data)
  const uint8_t local_hdr[] = {
      0x50, 0x4b, 0x03, 0x04,  // signature 0x04034b50
      0x14, 0x00,              // version needed 20
      0x00, 0x00,              // flags 0
      0x00, 0x00,              // compression method 0 (Stored)
      0x00, 0x00,              // mod time
      0x00, 0x00,              // mod date
      0x00, 0x00, 0x00, 0x00,  // crc32 (mocked)
      static_cast<uint8_t>(content.size() & 0xFF),
      static_cast<uint8_t>((content.size() >> 8) & 0xFF),
      0x00, 0x00,              // compressed size
      static_cast<uint8_t>(content.size() & 0xFF),
      static_cast<uint8_t>((content.size() >> 8) & 0xFF),
      0x00, 0x00,              // uncompressed size
      static_cast<uint8_t>(filename.size() & 0xFF),
      static_cast<uint8_t>((filename.size() >> 8) & 0xFF),  // name length
      0x00, 0x00               // extra field length
  };
  zip.insert(zip.end(), std::begin(local_hdr), std::end(local_hdr));
  zip.insert(zip.end(), filename.begin(), filename.end());
  zip.insert(zip.end(), content.begin(), content.end());

  // 2. Central Directory Header (46 字节 + name)
  const uint32_t cd_offset = static_cast<uint32_t>(zip.size());
  const uint8_t cd_hdr[] = {
      0x50, 0x4b, 0x01, 0x02,  // signature 0x02014b50
      0x14, 0x00,              // version made by 20
      0x14, 0x00,              // version needed 20
      0x00, 0x00,              // flags 0
      0x00, 0x00,              // compression method 0 (Stored)
      0x00, 0x00,              // mod time
      0x00, 0x00,              // mod date
      0x00, 0x00, 0x00, 0x00,  // crc32
      static_cast<uint8_t>(content.size() & 0xFF),
      static_cast<uint8_t>((content.size() >> 8) & 0xFF),
      0x00, 0x00,              // compressed size
      static_cast<uint8_t>(content.size() & 0xFF),
      static_cast<uint8_t>((content.size() >> 8) & 0xFF),
      0x00, 0x00,              // uncompressed size
      static_cast<uint8_t>(filename.size() & 0xFF),
      static_cast<uint8_t>((filename.size() >> 8) & 0xFF),  // name length
      0x00, 0x00,              // extra length
      0x00, 0x00,              // comment length
      0x00, 0x00,              // disk number start
      0x00, 0x00,              // internal file attributes
      0x00, 0x00, 0x00, 0x00,  // external file attributes
      static_cast<uint8_t>(local_offset & 0xFF),
      static_cast<uint8_t>((local_offset >> 8) & 0xFF),
      static_cast<uint8_t>((local_offset >> 16) & 0xFF),
      static_cast<uint8_t>((local_offset >> 24) & 0xFF)     // local header offset
  };
  zip.insert(zip.end(), std::begin(cd_hdr), std::end(cd_hdr));
  zip.insert(zip.end(), filename.begin(), filename.end());
  const uint32_t cd_size = static_cast<uint32_t>(zip.size() - cd_offset);

  // 3. End of Central Directory Record (EOCD, 22 字节)
  const uint8_t eocd[] = {
      0x50, 0x4b, 0x05, 0x06,  // signature 0x06054b50
      0x00, 0x00,              // disk number
      0x00, 0x00,              // start disk
      0x01, 0x00,              // entries on this disk (1)
      0x01, 0x00,              // total entries (1)
      static_cast<uint8_t>(cd_size & 0xFF),
      static_cast<uint8_t>((cd_size >> 8) & 0xFF),
      static_cast<uint8_t>((cd_size >> 16) & 0xFF),
      static_cast<uint8_t>((cd_size >> 24) & 0xFF),  // cd size
      static_cast<uint8_t>(cd_offset & 0xFF),
      static_cast<uint8_t>((cd_offset >> 8) & 0xFF),
      static_cast<uint8_t>((cd_offset >> 16) & 0xFF),
      static_cast<uint8_t>((cd_offset >> 24) & 0xFF),  // cd offset
      0x00, 0x00               // comment length
  };
  zip.insert(zip.end(), std::begin(eocd), std::end(eocd));

  return zip;
}

// 辅助函数：构造一个包含一个 Deflated 压缩文件的内存 ZIP 包
std::vector<uint8_t> createInMemoryDeflatedZip(
    const std::string& filename,
    const std::string& uncompressed_data
) {
  // 使用 raw deflate (windowBits = -15)
  z_stream strm{};
  deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
  std::vector<uint8_t> compressed(uncompressed_data.size() + 64);
  strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(uncompressed_data.data()));
  strm.avail_in = uncompressed_data.size();
  strm.next_out = compressed.data();
  strm.avail_out = compressed.size();
  deflate(&strm, Z_FINISH);
  compressed.resize(strm.total_out);
  deflateEnd(&strm);

  std::vector<uint8_t> zip;
  const uint32_t local_offset = static_cast<uint32_t>(zip.size());

  // 1. Local File Header
  const uint8_t local_hdr[] = {
      0x50, 0x4b, 0x03, 0x04,
      0x14, 0x00,
      0x00, 0x00,
      0x08, 0x00,              // compression method 8 (Deflated)
      0x00, 0x00,
      0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      static_cast<uint8_t>(compressed.size() & 0xFF),
      static_cast<uint8_t>((compressed.size() >> 8) & 0xFF),
      static_cast<uint8_t>((compressed.size() >> 16) & 0xFF),
      static_cast<uint8_t>((compressed.size() >> 24) & 0xFF),
      static_cast<uint8_t>(uncompressed_data.size() & 0xFF),
      static_cast<uint8_t>((uncompressed_data.size() >> 8) & 0xFF),
      static_cast<uint8_t>((uncompressed_data.size() >> 16) & 0xFF),
      static_cast<uint8_t>((uncompressed_data.size() >> 24) & 0xFF),
      static_cast<uint8_t>(filename.size() & 0xFF),
      static_cast<uint8_t>((filename.size() >> 8) & 0xFF),
      0x00, 0x00
  };
  zip.insert(zip.end(), std::begin(local_hdr), std::end(local_hdr));
  zip.insert(zip.end(), filename.begin(), filename.end());
  zip.insert(zip.end(), compressed.begin(), compressed.end());

  // 2. Central Directory Header
  const uint32_t cd_offset = static_cast<uint32_t>(zip.size());
  const uint8_t cd_hdr[] = {
      0x50, 0x4b, 0x01, 0x02,
      0x14, 0x00,
      0x14, 0x00,
      0x00, 0x00,
      0x08, 0x00,              // compression method 8
      0x00, 0x00,
      0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      static_cast<uint8_t>(compressed.size() & 0xFF),
      static_cast<uint8_t>((compressed.size() >> 8) & 0xFF),
      static_cast<uint8_t>((compressed.size() >> 16) & 0xFF),
      static_cast<uint8_t>((compressed.size() >> 24) & 0xFF),
      static_cast<uint8_t>(uncompressed_data.size() & 0xFF),
      static_cast<uint8_t>((uncompressed_data.size() >> 8) & 0xFF),
      static_cast<uint8_t>((uncompressed_data.size() >> 16) & 0xFF),
      static_cast<uint8_t>((uncompressed_data.size() >> 24) & 0xFF),
      static_cast<uint8_t>(filename.size() & 0xFF),
      static_cast<uint8_t>((filename.size() >> 8) & 0xFF),
      0x00, 0x00,
      0x00, 0x00,
      0x00, 0x00,
      0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      static_cast<uint8_t>(local_offset & 0xFF),
      static_cast<uint8_t>((local_offset >> 8) & 0xFF),
      static_cast<uint8_t>((local_offset >> 16) & 0xFF),
      static_cast<uint8_t>((local_offset >> 24) & 0xFF)
  };
  zip.insert(zip.end(), std::begin(cd_hdr), std::end(cd_hdr));
  zip.insert(zip.end(), filename.begin(), filename.end());
  const uint32_t cd_size = static_cast<uint32_t>(zip.size() - cd_offset);

  // 3. EOCD
  const uint8_t eocd[] = {
      0x50, 0x4b, 0x05, 0x06,
      0x00, 0x00,
      0x00, 0x00,
      0x01, 0x00,
      0x01, 0x00,
      static_cast<uint8_t>(cd_size & 0xFF),
      static_cast<uint8_t>((cd_size >> 8) & 0xFF),
      static_cast<uint8_t>((cd_size >> 16) & 0xFF),
      static_cast<uint8_t>((cd_size >> 24) & 0xFF),
      static_cast<uint8_t>(cd_offset & 0xFF),
      static_cast<uint8_t>((cd_offset >> 8) & 0xFF),
      static_cast<uint8_t>((cd_offset >> 16) & 0xFF),
      static_cast<uint8_t>((cd_offset >> 24) & 0xFF),
      0x00, 0x00
  };
  zip.insert(zip.end(), std::begin(eocd), std::end(eocd));

  return zip;
}

}  // namespace

TEST(ZipArchiveTest, OpenInvalidBuffers) {
  // 空缓冲区
  EXPECT_FALSE(ZipArchive::openMemory({}).has_value());

  // 小于 22 字节
  std::vector<uint8_t> small_data(21, 0);
  EXPECT_FALSE(ZipArchive::openMemory(small_data).has_value());

  // 缺少 EOCD 签名的垃圾数据
  std::vector<uint8_t> garbage(100, 0xAA);
  EXPECT_FALSE(ZipArchive::openMemory(garbage).has_value());
}

TEST(ZipArchiveTest, ReadStoredEntry) {
  const std::string name = "hello.txt";
  const std::string content = "Hello Minecraft Lobby Engine!";
  auto zip_data = createInMemoryStoredZip(name, content);

  auto archive = ZipArchive::openMemory(zip_data);
  ASSERT_TRUE(archive.has_value());

  EXPECT_EQ(archive->entries().size(), 1);
  EXPECT_TRUE(archive->hasEntry(name));
  EXPECT_FALSE(archive->hasEntry("not_exist.txt"));

  auto ext_match = archive->findFirstByExtension(".txt");
  ASSERT_TRUE(ext_match.has_value());
  EXPECT_EQ(*ext_match, name);

  auto extracted = archive->extractEntry(name);
  ASSERT_TRUE(extracted.has_value());
  std::string extracted_str(extracted->begin(), extracted->end());
  EXPECT_EQ(extracted_str, content);
}

TEST(ZipArchiveTest, ReadDeflatedEntry) {
  const std::string name = "level.dat";
  std::string content;
  for (int i = 0; i < 50; ++i) {
    content += "Repeated block data payload for deflation testing 1234567890\n";
  }

  auto zip_data = createInMemoryDeflatedZip(name, content);
  auto archive = ZipArchive::openMemory(zip_data);
  ASSERT_TRUE(archive.has_value());

  EXPECT_TRUE(archive->hasEntry(name));
  auto extracted = archive->extractEntry(name);
  ASSERT_TRUE(extracted.has_value());
  EXPECT_EQ(extracted->size(), content.size());
  std::string extracted_str(extracted->begin(), extracted->end());
  EXPECT_EQ(extracted_str, content);
}

TEST(ZipArchiveTest, NonExistentEntryHandling) {
  auto zip_data = createInMemoryStoredZip("foo.txt", "bar");
  auto archive = ZipArchive::openMemory(zip_data);
  ASSERT_TRUE(archive.has_value());

  EXPECT_FALSE(archive->extractEntry("missing.txt").has_value());
  EXPECT_FALSE(archive->findFirstByExtension(".dat").has_value());
}
