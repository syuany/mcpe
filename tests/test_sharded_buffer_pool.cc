#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "common/BufferPool.h"
#include "common/ShardedBufferPool.h"

using namespace mcpe;

// 1. 基础功能测试：借出、写入、RAII 自动归还与计数统计
TEST(ShardedBufferPoolTest, BasicAllocateAndDeallocate) {
  ShardedBufferPool pool(4, 32);
  EXPECT_EQ(pool.shardCount(), 4);
  EXPECT_EQ(pool.totalCapacity(), 128);
  EXPECT_EQ(pool.totalAllocatedCount(), 0);

  {
    auto block = pool.allocate();
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(pool.totalAllocatedCount(), 1);

    // 写入与读取验证
    std::memset(block.get(), 0x5A, BufferPool::kBlockSize);
    EXPECT_EQ(static_cast<uint8_t>(block[0]), 0x5A);
    EXPECT_EQ(static_cast<uint8_t>(block[4095]), 0x5A);
  }

  // 离开作用域自动归还，分配计数归零
  EXPECT_EQ(pool.totalAllocatedCount(), 0);
}

// 2. 工作窃取特性验证 (Work Stealing)
// 单个线程借出的块数超过单一分片的物理容量，验证是否自动从其他空闲分片“窃取”内存块而未触发系统堆分配
TEST(ShardedBufferPoolTest, CrossShardWorkStealing) {
  constexpr size_t kShards = 4;
  constexpr size_t kCapPerShard = 5; // 每分片仅 5 块，全池共 20 块
  ShardedBufferPool pool(kShards, kCapPerShard);

  // 单一线程借出 12 块（已大幅突破该线程对应分片的 5 块上限）
  std::vector<PooledBlock> blocks;
  blocks.reserve(12);

  for (size_t i = 0; i < 12; ++i) {
    auto b = pool.allocate();
    ASSERT_NE(b, nullptr);
    b[0] = static_cast<char>('A' + i);
    blocks.push_back(std::move(b));
  }

  EXPECT_EQ(pool.totalAllocatedCount(), 12);

  // 验证借出的所有块均来自池内物理 Arena（未触发系统 ::operator new 降级）
  for (const auto& b : blocks) {
    bool in_any_arena = false;
    for (size_t s = 0; s < kShards; ++s) {
      if (pool.getShard(s).isInArena(b.get())) {
        in_any_arena = true;
        break;
      }
    }
    EXPECT_TRUE(in_any_arena);
  }

  blocks.clear();
  EXPECT_EQ(pool.totalAllocatedCount(), 0);
}

// 3. 跨线程归还测试 (Cross-Thread Deallocation)
// 线程 A 从自己的分片借出，移交给线程 B 释放，验证无锁精准归还原所属分片
TEST(ShardedBufferPoolTest, CrossThreadDeallocation) {
  ShardedBufferPool pool(4, 16);
  constexpr int kCount = 50;

  for (int i = 0; i < kCount; ++i) {
    PooledBlock block_from_thread_a{nullptr};

    std::thread producer([&pool, &block_from_thread_a]() {
      block_from_thread_a = pool.allocate();
      block_from_thread_a[0] = 'X';
    });
    producer.join();

    EXPECT_EQ(pool.totalAllocatedCount(), 1);

    std::thread consumer([block = std::move(block_from_thread_a)]() mutable {
      EXPECT_EQ(block[0], 'X');
      block.reset(); // 在另一个线程中执行归还
    });
    consumer.join();

    EXPECT_EQ(pool.totalAllocatedCount(), 0);
  }
}

// 4. 高并发多线程压力与数据隔离测试
TEST(ShardedBufferPoolTest, HighConcurrencyPressure) {
  ShardedBufferPool pool(8, 64);
  constexpr int kThreads = 8;
  constexpr int kIterations = 10000;

  std::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&pool, t]() {
      char magic = static_cast<char>('0' + (t % 10));
      for (int i = 0; i < kIterations; ++i) {
        auto b1 = pool.allocate();
        b1[0] = magic;
        b1[4095] = magic;

        auto b2 = pool.allocate();
        b2[0] = static_cast<char>(magic + 1);

        EXPECT_EQ(b1[0], magic);
        EXPECT_EQ(b1[4095], magic);
        EXPECT_EQ(b2[0], static_cast<char>(magic + 1));
      }
    });
  }

  for (auto& w : workers) {
    w.join();
  }

  EXPECT_EQ(pool.totalAllocatedCount(), 0);
}
