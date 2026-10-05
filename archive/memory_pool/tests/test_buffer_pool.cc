#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "common/BufferPool.h"

using namespace mcpe;

// 1. 基础借还与数据写入测试（验证 4KB 空间绝对可读写且不越界）
TEST(BufferPoolTest, BasicAllocateAndWrite) {
  auto& pool = BufferPool::instance();
  size_t init_alloc = pool.allocatedCount();

  {
    auto block = pool.allocate();
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(pool.allocatedCount(), init_alloc + 1);

    // 填满数据并校验完整性
    std::memset(block.get(), 0x5A, BufferPool::kBlockSize);
    for (size_t i = 0; i < BufferPool::kBlockSize; ++i) {
      ASSERT_EQ(static_cast<uint8_t>(block[i]), 0x5A);
    }
  }

  // 离开作用域，RAII 触发归还
  EXPECT_EQ(pool.allocatedCount(), init_alloc);
}

// 2. 弹性降级 (Fallback) 测试：借穿预设容量，验证自动扩容与归还
TEST(BufferPoolTest, FallbackExceedsCapacity) {
  BufferPool small_pool(4, /*allow_dynamic_expansion=*/false);  // 仅分配 4 块的小池子
  std::vector<PooledBlock> holders;

  // 借出 10 块（前 4 块走池内，后 6 块走 Fallback）
  for (int i = 0; i < 10; ++i) {
    holders.push_back(small_pool.allocate());
    ASSERT_NE(holders.back(), nullptr);
  }

  EXPECT_EQ(small_pool.allocatedCount(), 10);

  // 清空全部归还（池内块回栈，溢出块调用 delete，验证 0 崩溃 0 泄漏）
  holders.clear();
  EXPECT_EQ(small_pool.allocatedCount(), 0);
}

// 3. 💥 核心压测：16 个并发线程同时疯狂争抢借还（每线程 50,000 次，共 80 万次无锁流转）
TEST(BufferPoolTest, HighConcurrencyMultiThreadedStress) {
  auto& pool = BufferPool::instance();
  constexpr int kThreads = 16;
  constexpr int kIterations = 50000;

  std::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&pool]() {
      for (int i = 0; i < kIterations; ++i) {
        auto block = pool.allocate();
        // 模拟真实网络操作：在内存前中后随机写入数据
        block[0] = 'M';
        block[2048] = 'C';
        block[4095] = 'P';
        ASSERT_EQ(block[0], 'M');
        ASSERT_EQ(block[2048], 'C');
        ASSERT_EQ(block[4095], 'P');
      }
    });
  }

  for (auto& w : workers) {
    w.join();
  }

  // 压力测试结束，验证池内分配计数完美归零，无锁原子操作 100% 准确！
  EXPECT_EQ(pool.allocatedCount(), 0);
}

// 4. 针对 ABA 漏洞的交替高频借还与数据隔离校验测试
TEST(BufferPoolTest, AbaImmunityAndMemoryIsolation) {
  // 建立一个仅有 2 块容量的微型池，极度放大 ABA 出现的概率
  BufferPool pool(2, /*allow_dynamic_expansion=*/false);
  constexpr int kThreads = 8;
  constexpr int kIterations = 20000;

  std::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&pool, t]() {
      char magic = static_cast<char>('A' + (t % 26));
      for (int i = 0; i < kIterations; ++i) {
        auto b1 = pool.allocate();
        b1[0] = magic;
        b1[4095] = magic;

        // 快速借出第二个块，迫使两个块反复交替进出栈
        auto b2 = pool.allocate();
        b2[0] = static_cast<char>(magic + 1);

        // 校验数据未被其他并发线程污染
        ASSERT_EQ(b1[0], magic);
        ASSERT_EQ(b1[4095], magic);
        ASSERT_EQ(b2[0], static_cast<char>(magic + 1));
      }
    });
  }

  for (auto& w : workers) {
    w.join();
  }

  EXPECT_EQ(pool.allocatedCount(), 0);
}

// 6. 现象具体化测试 ②：洪峰超额借还 同一场景下三种方案对比 (方案 A/B/C)
// 同一测试场景：8 线程并发突发洪峰（每线程突发持有 500 块 x 300 轮，共 120 万次 4KB 页面分配/归还）
// 对比三种不同分配机制的性能表现：
//   方案 A (传统降级方案): 借穿预设容量后退化为散碎系统堆分配 (::operator new/delete 裸调)
//   方案 B (理想基准方案): 提前静态全量预分配连续 Arena (依赖人工预估，容易浪费或不足)
//   方案 C (本次重构方案): 突破预分配容量时按 4MB 批量动态无锁挂载新 Chunk (二级连续 Arena 链)
TEST(BufferPoolTest, FallbackPerformanceCliff) {
#if defined(__GLIBC__)
  malloc_trim(0);  // 清理前序用例释放的空闲堆缓存，模拟真实突发高负载
#endif

  constexpr int kThreads = 8;
  constexpr int kBatch = 500;  // 突破 glibc 默认 tcache 缓存上限与 2MB 阈值，直击系统堆锁
  constexpr int kRounds = 300;  // 300 轮突发，共 120 万次借还

  // 方案 B (理想基准方案): 启动前全量预分配充足容量，走单池连续 Arena 快路径
  BufferPool in_pool(kThreads * kBatch + 100);
  auto start_pool = std::chrono::steady_clock::now();
  {
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([&in_pool]() {
        for (int r = 0; r < kRounds; ++r) {
          std::vector<PooledBlock> holder;
          holder.reserve(kBatch);
          for (int i = 0; i < kBatch; ++i) {
            holder.push_back(in_pool.allocate());
            holder.back()[0] = 'P';
            asm volatile("" : : "r"(holder.back().get()) : "memory");
          }
          holder.clear();
        }
      });
    }
    for (auto& w : workers) w.join();
  }
  auto elapsed_pool = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start_pool
  )
                          .count();

  // 方案 A (传统降级方案): 内存池预分配容量被彻底借光，全部请求走 Fallback (::operator new/delete
  // 散碎分配)
  BufferPool exhausted_pool(1, /*allow_dynamic_expansion=*/false);
  auto permanent_holder = exhausted_pool.allocate();  // 借出唯一一块，使池子彻底空栈
  auto start_fallback = std::chrono::steady_clock::now();
  {
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([&exhausted_pool]() {
        for (int r = 0; r < kRounds; ++r) {
          std::vector<PooledBlock> holder;
          holder.reserve(kBatch);
          for (int i = 0; i < kBatch; ++i) {
            holder.push_back(exhausted_pool.allocate());
            holder.back()[0] = 'F';
            asm volatile("" : : "r"(holder.back().get()) : "memory");
          }
          holder.clear();
        }
      });
    }
    for (auto& w : workers) w.join();
  }
  auto elapsed_fallback = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start_fallback
  )
                              .count();

  // 方案 C (本次重构方案): 突破预分配容量时按 4MB 批量动态无锁挂载新 Chunk (二级连续 Arena 链)
  BufferPool dynamic_pool(1, /*allow_dynamic_expansion=*/true);
  auto permanent_dynamic = dynamic_pool.allocate();  // 借空初始 1 块
  auto start_dynamic = std::chrono::steady_clock::now();
  {
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([&dynamic_pool]() {
        for (int r = 0; r < kRounds; ++r) {
          std::vector<PooledBlock> holder;
          holder.reserve(kBatch);
          for (int i = 0; i < kBatch; ++i) {
            holder.push_back(dynamic_pool.allocate());
            holder.back()[0] = 'D';
            asm volatile("" : : "r"(holder.back().get()) : "memory");
          }
          holder.clear();
        }
      });
    }
    for (auto& w : workers) w.join();
  }
  auto elapsed_dynamic = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start_dynamic
  )
                             .count();

  double speedup_dynamic = (double)elapsed_fallback / std::max<long long>(elapsed_dynamic, 1);
  double ratio_prealloc = (double)elapsed_dynamic / std::max<long long>(elapsed_pool, 1);

  std::cout
      << "\n================ [ PERF 基准实验: 洪峰超额借还 同一场景下三种方案对比 ] "
         "================\n"
      << "  同一压测场景: 8 线程并发突发 x 每线程 500 块 x 300 轮 (共 1,200,000 次 4KB 页面借还)\n"
      << "  ------------------------------------------------------------------------------------\n"
      << "  方案 A (传统降级方案: 散碎系统堆分配)  : " << elapsed_fallback << " ms ("
      << (double)elapsed_fallback / std::max<long long>(elapsed_pool, 1) << "x 暴跌性能悬崖)\n"
      << "  方案 B (理想基准方案: 静态全量预分配)  : " << elapsed_pool << " ms\n"
      << "  方案 C (本次重构方案: 动态 4MB Chunk 扩容): " << elapsed_dynamic << " ms ("
      << speedup_dynamic << "x 消除性能悬崖加速, 较静态预分配开销比率仅 " << ratio_prealloc
      << "x)\n"
      << "  动态挂载 4MB Chunk 数量                   : " << dynamic_pool.chunkCount() << " 个\n"
      << "======================================================================================"
         "\n\n";

  EXPECT_EQ(in_pool.allocatedCount(), 0);
  EXPECT_EQ(exhausted_pool.allocatedCount(), 1);  // 仅剩 permanent_holder
  EXPECT_EQ(dynamic_pool.allocatedCount(), 1);    // 仅剩 permanent_dynamic
  EXPECT_GT(dynamic_pool.chunkCount(), 1);
  EXPECT_LT(elapsed_dynamic, elapsed_fallback);
}

// 5. 现象具体化测试 ①：全局单栈顶争用瓶颈 (Global Head Bottleneck)
// 对比 8 个并发线程争抢同一个全局 head_，与 8 个并发线程各自分片 (Sharded) 访问独立 pool 的性能差异
TEST(BufferPoolTest, GlobalHeadContentionBottleneck) {
  constexpr int kThreads = 8;
  constexpr int kIterations = 100000;  // 8 线程共计 80 万次借还

  // 场景 A: 8 个并发线程高频狂争同一个全局 head_
  BufferPool single_contended_pool(1024);
  auto start_contended = std::chrono::steady_clock::now();
  {
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([&single_contended_pool]() {
        for (int i = 0; i < kIterations; ++i) {
          auto block = single_contended_pool.allocate();
          block[0] = 'C';
          asm volatile("" : : "r"(block.get()) : "memory");
        }
      });
    }
    for (auto& w : workers) w.join();
  }
  auto elapsed_contended = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start_contended
  )
                               .count();

  // 场景 B: 8 个并发线程分别访问 8 个独立的分片 pool (模拟 Sharded / Thread-Local
  // Pool，消除单点争用)
  std::vector<std::unique_ptr<BufferPool>> sharded_pools;
  for (int i = 0; i < kThreads; ++i) {
    sharded_pools.push_back(std::make_unique<BufferPool>(128));
  }
  auto start_sharded = std::chrono::steady_clock::now();
  {
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([&sharded_pools, t]() {
        for (int i = 0; i < kIterations; ++i) {
          auto block = sharded_pools[t]->allocate();
          block[0] = 'S';
          asm volatile("" : : "r"(block.get()) : "memory");
        }
      });
    }
    for (auto& w : workers) w.join();
  }
  auto elapsed_sharded = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start_sharded
  )
                             .count();

  std::cout << "\n================ [ PERF 瓶颈实验 ①: 单 Head 争用瓶颈 ] ================\n"
            << "  8 线程争抢单 Head 耗时 : " << elapsed_contended << " ms\n"
            << "  8 线程分片多 Head 耗时 : " << elapsed_sharded << " ms\n"
            << "  分片消除总线风暴加速比 : "
            << (double)elapsed_contended / std::max<long long>(elapsed_sharded, 1) << "x\n"
            << "========================================================================\n\n";

  EXPECT_EQ(single_contended_pool.allocatedCount(), 0);
  for (auto& p : sharded_pools) {
    EXPECT_EQ(p->allocatedCount(), 0);
  }
}

// 7. 动态 Chunk 扩容基本功能测试：初始容量耗尽后自动按 4MB 挂载新 Chunk
TEST(BufferPoolTest, DynamicChunkExpansionBasic) {
  BufferPool pool(4);  // 初始仅预分配 4 块
  EXPECT_EQ(pool.capacity(), 4);
  EXPECT_EQ(pool.chunkCount(), 1);
  EXPECT_EQ(pool.allocatedCount(), 0);

  std::vector<PooledBlock> blocks;
  blocks.reserve(10);

  // 借出 10 块（前 4 块来自 Chunk 0，后 6 块自动触发动态挂载 4MB Chunk 1）
  for (int i = 0; i < 10; ++i) {
    auto b = pool.allocate();
    ASSERT_NE(b, nullptr);
    b[0] = static_cast<char>('A' + i);
    b[4095] = static_cast<char>('Z' - i);
    blocks.push_back(std::move(b));
  }

  // 验证容量与 Chunk 链表状态
  EXPECT_EQ(pool.allocatedCount(), 10);
  EXPECT_EQ(pool.chunkCount(), 2);
  EXPECT_EQ(pool.capacity(), 4 + BufferPool::kBlocksPerChunk);  // 4 + 1024 = 1028

  // 验证所有 10 块的血统均在 Arena 中（前 4 块在初始 Arena，后 6 块在二级动态 Chunk 中）
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(pool.isInArena(blocks[i].get()));
    EXPECT_EQ(blocks[i][0], static_cast<char>('A' + i));
    EXPECT_EQ(blocks[i][4095], static_cast<char>('Z' - i));
  }

  // 全部归还
  blocks.clear();
  EXPECT_EQ(pool.allocatedCount(), 0);
  // 归还后 Chunk 空间不销毁，保持扩容后的容量供后续复用
  EXPECT_EQ(pool.capacity(), 4 + BufferPool::kBlocksPerChunk);
}

// 8. 跨多个 4MB Chunk 连续动态扩容与数据完整性校验
TEST(BufferPoolTest, DynamicChunkExpansionMultiChunk) {
  BufferPool pool(4);
  constexpr size_t kTotalBlocks = 2500;  // 需要至少 1 (初始) + 3 个动态 4MB Chunk (1024 * 3 = 3072)
  std::vector<PooledBlock> blocks;
  blocks.reserve(kTotalBlocks);

  for (size_t i = 0; i < kTotalBlocks; ++i) {
    auto b = pool.allocate();
    ASSERT_NE(b, nullptr);
    // 写入唯一标识以校验数据隔离
    *reinterpret_cast<size_t*>(b.get()) = i;
    blocks.push_back(std::move(b));
  }

  EXPECT_EQ(pool.allocatedCount(), kTotalBlocks);
  EXPECT_GE(pool.chunkCount(), 4);  // 初始 1 个 + 至少 3 个动态 Chunk
  EXPECT_GE(pool.capacity(), 4 + 3 * BufferPool::kBlocksPerChunk);

  // 校验每个块的数据完整性与 isInArena 属性
  for (size_t i = 0; i < kTotalBlocks; ++i) {
    EXPECT_TRUE(pool.isInArena(blocks[i].get()));
    EXPECT_EQ(*reinterpret_cast<size_t*>(blocks[i].get()), i);
  }

  blocks.clear();
  EXPECT_EQ(pool.allocatedCount(), 0);
}

// 9. 多线程高并发并发触发 Chunk 扩容与无锁挂载稳定性压测
TEST(BufferPoolTest, DynamicChunkExpansionConcurrentMountStress) {
  BufferPool pool(2);  // 仅 2 块初始容量，迫使 16 线程在启动瞬间发生重度并发扩容竞争
  constexpr int kThreads = 16;
  constexpr int kBlocksPerThread = 150;  // 共计 2400 块，需要挂载多个 4MB Chunk

  std::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&pool, t]() {
      std::vector<PooledBlock> thread_blocks;
      thread_blocks.reserve(kBlocksPerThread);

      for (int i = 0; i < kBlocksPerThread; ++i) {
        auto b = pool.allocate();
        ASSERT_NE(b, nullptr);
        // 写入线程特征数据
        b[0] = static_cast<char>('A' + (t % 26));
        b[4095] = static_cast<char>('0' + (i % 10));
        thread_blocks.push_back(std::move(b));
      }

      // 验证本线程持有的所有块数据未被并发冲撞或踩内存
      for (int i = 0; i < kBlocksPerThread; ++i) {
        ASSERT_TRUE(pool.isInArena(thread_blocks[i].get()));
        ASSERT_EQ(thread_blocks[i][0], static_cast<char>('A' + (t % 26)));
        ASSERT_EQ(thread_blocks[i][4095], static_cast<char>('0' + (i % 10)));
      }

      thread_blocks.clear();
    });
  }

  for (auto& w : workers) {
    w.join();
  }

  // 校验并发借还归零，无锁计数与状态完全吻合
  EXPECT_EQ(pool.allocatedCount(), 0);
  EXPECT_GT(pool.chunkCount(), 1);
}
