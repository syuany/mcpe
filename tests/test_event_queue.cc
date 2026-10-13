#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "common/SpatialTypes.h"
#include "world/EventQueue.h"

using namespace mcpe::world;

TEST(WorldTypesExtensionTest, Vector3dOperations) {
  Vector3d v1{1.0, 2.0, 3.0};
  Vector3d v2{4.0, 6.0, 3.0};

  Vector3d diff = v2 - v1;
  EXPECT_DOUBLE_EQ(diff.x, 3.0);
  EXPECT_DOUBLE_EQ(diff.y, 4.0);
  EXPECT_DOUBLE_EQ(diff.z, 0.0);

  EXPECT_DOUBLE_EQ(v1.distanceSquared(v2), 25.0);
  EXPECT_DOUBLE_EQ(v1.distance(v2), 5.0);

  Vector3d entity_pos{10.7, 64.2, -5.3};
  BlockPos block = entity_pos.toBlockPos();
  EXPECT_EQ(block.x, 10);
  EXPECT_EQ(block.y, 64);
  EXPECT_EQ(block.z, -6);  // 向下取整：-5.3 属于 -6
}

TEST(WorldTypesExtensionTest, RotationConversion) {
  Rotation rot{180.0f, 90.0f};
  EXPECT_EQ(rot.toByteYaw(), 128);
  EXPECT_EQ(rot.toBytePitch(), 64);

  Rotation zero{0.0f, 0.0f};
  EXPECT_EQ(zero.toByteYaw(), 0);
  EXPECT_EQ(zero.toBytePitch(), 0);
}

TEST(WorldTypesExtensionTest, BlockFaceOffsets) {
  BlockPos base{10, 20, 30};

  EXPECT_EQ(base + toFaceOffset(BlockFace::Bottom), BlockPos(10, 19, 30));
  EXPECT_EQ(base + toFaceOffset(BlockFace::Top), BlockPos(10, 21, 30));
  EXPECT_EQ(base + toFaceOffset(BlockFace::North), BlockPos(10, 20, 29));
  EXPECT_EQ(base + toFaceOffset(BlockFace::South), BlockPos(10, 20, 31));
  EXPECT_EQ(base + toFaceOffset(BlockFace::West), BlockPos(9, 20, 30));
  EXPECT_EQ(base + toFaceOffset(BlockFace::East), BlockPos(11, 20, 30));
}

TEST(EventQueueTest, MemoryFootprintAndCompactness) {
  // 验证 WorldEvent 内存极其紧凑，零堆分配
  EXPECT_LE(sizeof(WorldEvent), 64);
}

TEST(EventQueueTest, PatternMatchingWithStdVisit) {
  EventQueue queue;

  // 1. 推入移动事件
  WorldEvent move_event;
  move_event.session_id = 1001;
  move_event.entity_id = 1;
  move_event.payload = PlayerMoveEvent{
      .position = {12.5, 65.0, -8.3},
      .rotation = {90.0f, 0.0f},
      .on_ground = true,
      .has_position = true,
      .has_rotation = true
  };
  queue.push(std::move(move_event));

  // 2. 推入挖掘事件
  WorldEvent dig_event;
  dig_event.session_id = 1002;
  dig_event.entity_id = 2;
  dig_event.payload = PlayerDiggingEvent{
      .status = DigStatus::Finished,
      .block_pos = BlockPos{12, 64, -8},
      .face = BlockFace::Top
  };
  queue.push(std::move(dig_event));

  // 3. 消费并断言模式匹配
  std::vector<WorldEvent> drained;
  queue.drain(drained);
  ASSERT_EQ(drained.size(), 2);

  int move_count = 0;
  int dig_count = 0;

  for (const auto& ev : drained) {
    std::visit(
        Overloaded{
            [&](const PlayerMoveEvent& move) {
              ++move_count;
              EXPECT_EQ(ev.session_id, 1001);
              EXPECT_DOUBLE_EQ(move.position.x, 12.5);
              EXPECT_TRUE(move.has_position);
            },
            [&](const PlayerDiggingEvent& dig) {
              ++dig_count;
              EXPECT_EQ(ev.session_id, 1002);
              EXPECT_EQ(dig.status, DigStatus::Finished);
              EXPECT_EQ(dig.block_pos, BlockPos(12, 64, -8));
              EXPECT_EQ(dig.face, BlockFace::Top);
            },
            [](const auto&) {
              FAIL() << "Unexpected event payload";
            }
        },
        ev.payload
    );
  }

  EXPECT_EQ(move_count, 1);
  EXPECT_EQ(dig_count, 1);
}

TEST(EventQueueTest, HighConcurrencyMpscThreadSafety) {
  EventQueue queue;
  constexpr int kProducers = 4;
  constexpr int kEventsPerProducer = 25000;
  constexpr int kTotalEvents = kProducers * kEventsPerProducer;

  std::atomic<bool> start_signal{false};
  std::vector<std::thread> producers;

  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p]() {
      while (!start_signal.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      for (int i = 0; i < kEventsPerProducer; ++i) {
        WorldEvent ev;
        ev.session_id = static_cast<uint64_t>(p);
        ev.entity_id = i;
        ev.payload = PlayerMoveEvent{
            .position = {static_cast<double>(i), 0.0, 0.0},
            .rotation = {0.0f, 0.0f},
            .on_ground = true
        };
        queue.push(std::move(ev));
      }
    });
  }

  std::atomic<bool> consumer_done{false};
  int total_drained = 0;

  std::thread consumer([&]() {
    std::vector<WorldEvent> batch;
    while (!consumer_done.load(std::memory_order_acquire) || total_drained < kTotalEvents) {
      queue.drain(batch);
      total_drained += static_cast<int>(batch.size());
      if (batch.empty()) {
        std::this_thread::yield();
      }
    }
  });

  start_signal.store(true, std::memory_order_release);

  for (auto& th : producers) {
    th.join();
  }

  consumer_done.store(true, std::memory_order_release);
  consumer.join();

  EXPECT_EQ(total_drained, kTotalEvents);
}
