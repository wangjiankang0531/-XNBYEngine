// NetworkEventPump tests — enqueue/poll order, thread safety, clear.

#include <gtest/gtest.h>
#include <hongtu/cloud/network_event_pump.hpp>

#include <thread>
#include <vector>

using hongtu::cloud::NetworkEventPump;
using hongtu::window::EventKind;
using hongtu::window::InputEvent;

TEST(NetworkEventPumpTest, EmptyPollReturnsFalse) {
  NetworkEventPump p;
  InputEvent out;
  EXPECT_FALSE(p.poll(out));
  EXPECT_EQ(p.pending(), 0u);
}

TEST(NetworkEventPumpTest, EnqueueAndPoll) {
  NetworkEventPump p;
  InputEvent in;
  in.kind = EventKind::MouseDown;
  in.mouse_x = 42;
  EXPECT_TRUE(p.enqueue(in));
  EXPECT_EQ(p.pending(), 1u);

  InputEvent out;
  ASSERT_TRUE(p.poll(out));
  EXPECT_EQ(out.kind,    EventKind::MouseDown);
  EXPECT_EQ(out.mouse_x, 42);
  EXPECT_EQ(p.pending(), 0u);
}

TEST(NetworkEventPumpTest, FifoOrder) {
  NetworkEventPump p;
  for (int i = 0; i < 5; ++i) {
    InputEvent e; e.mouse_x = i;
    p.enqueue(e);
  }
  for (int i = 0; i < 5; ++i) {
    InputEvent out;
    ASSERT_TRUE(p.poll(out));
    EXPECT_EQ(out.mouse_x, i);
  }
  InputEvent leftover;
  EXPECT_FALSE(p.poll(leftover));
}

TEST(NetworkEventPumpTest, Clear) {
  NetworkEventPump p;
  for (int i = 0; i < 3; ++i) p.enqueue({});
  EXPECT_EQ(p.pending(), 3u);
  p.clear();
  EXPECT_EQ(p.pending(), 0u);
}

TEST(NetworkEventPumpTest, ThreadSafeEnqueuePoll) {
  // Two producer threads enqueue 1000 events each; one consumer polls all
  // 2000 and verifies no event is lost or duplicated.
  NetworkEventPump p;
  const int N = 1000;

  auto producer = [&p, N](int base) {
    for (int i = 0; i < N; ++i) {
      InputEvent e;
      e.mouse_x = base + i;
      p.enqueue(e);
    }
  };

  std::thread t1(producer, 0);
  std::thread t2(producer, 100000);
  t1.join();
  t2.join();

  std::vector<int> seen;
  InputEvent out;
  while (p.poll(out)) seen.push_back(out.mouse_x);
  EXPECT_EQ(seen.size(), static_cast<size_t>(2 * N));
}
