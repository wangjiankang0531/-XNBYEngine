#include <gtest/gtest.h>
#include <hongtu/core/net/download_queue.hpp>
#include <hongtu/core/net/http_client.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace hongtu::core::net;

// ============================================================================
// HttpClient Tests
// ============================================================================

class HttpClientTest : public ::testing::Test {
protected:
  HttpClient client;
};

TEST_F(HttpClientTest, Construction) {
  // HttpClient should construct and destruct without error.
  SUCCEED();
}

TEST_F(HttpClientTest, SetTimeout) {
  EXPECT_NO_THROW(client.setTimeout(10));
}

TEST_F(HttpClientTest, SyncGetInvalidUrl) {
  auto response = client.get("http://invalid.invalid.invalid/");
  // Should fail gracefully -- either DNS resolution error or connection error.
  EXPECT_FALSE(response.success());
  EXPECT_FALSE(response.error.empty());
}

TEST_F(HttpClientTest, SyncGetEmptyUrl) {
  auto response = client.get("");
  EXPECT_FALSE(response.success());
}

TEST_F(HttpClientTest, SyncGetHttpBin) {
  // Use a well-known public URL. Network may not be available, so be lenient.
  client.setTimeout(5);
  auto response = client.get("http://httpbin.org/get");
  // We only check structure, not require success (network might be unavailable).
  if (response.success()) {
    EXPECT_EQ(response.status_code, 200);
    EXPECT_FALSE(response.body.empty());
    EXPECT_TRUE(response.error.empty());
  } else {
    // Network unavailable -- just ensure we got a proper error.
    EXPECT_NE(response.status_code, 200);
  }
}

TEST_F(HttpClientTest, AsyncGetAndUpdate) {
  std::atomic<bool> called{false};
  client.setTimeout(5);

  auto id = client.getAsync("http://invalid.invalid.invalid/", [&called](HttpClient::Response response) {
    HT_UNUSED(response);
    called.store(true);
  });
  HT_UNUSED(id);

  // Wait a bit for the async request to complete.
  for (int i = 0; i < 100 && !called.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client.update();
  }

  EXPECT_TRUE(called.load());
}

TEST_F(HttpClientTest, CancelRequest) {
  std::atomic<bool> called{false};
  client.setTimeout(30);

  auto id = client.getAsync("http://httpbin.org/delay/10", [&called](HttpClient::Response response) {
    HT_UNUSED(response);
    called.store(true);
  });

  // Cancel immediately.
  client.cancelRequest(id);

  // Give a moment, then update.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  client.update();

  // Callback should not have been called (cancelled).
  EXPECT_FALSE(called.load());
}

TEST_F(HttpClientTest, ResponseSuccessHelper) {
  HttpClient::Response r;
  r.status_code = 200;
  r.transfer_completed = true;
  EXPECT_TRUE(r.success());

  r.status_code = 404;
  EXPECT_FALSE(r.success());

  r.status_code = 0;
  EXPECT_TRUE(r.success()); // file:// and other non-HTTP curl transports

  r.status_code = 299;
  EXPECT_TRUE(r.success());

  r.status_code = 300;
  EXPECT_FALSE(r.success());

  r.status_code = 200;
  r.transfer_completed = false;
  EXPECT_FALSE(r.success());

  r.transfer_completed = true;
  r.error = "transport error";
  EXPECT_FALSE(r.success());
}

// ============================================================================
// DownloadQueue Tests
// ============================================================================

class DownloadQueueTest : public ::testing::Test {
protected:
  HttpClient client;
};

TEST_F(DownloadQueueTest, Construction) {
  DownloadQueue queue(client);
  EXPECT_EQ(queue.getPendingCount(), 0u);
  EXPECT_EQ(queue.getActiveCount(), 0u);
}

TEST_F(DownloadQueueTest, SetMaxConcurrent) {
  DownloadQueue queue(client);
  EXPECT_NO_THROW(queue.setMaxConcurrent(4));
}

TEST_F(DownloadQueueTest, EnqueueIncreasesPending) {
  DownloadQueue queue(client);

  queue.enqueue("http://example.com/a", DownloadQueue::Priority::Normal, [](HttpClient::Response) {});
  EXPECT_EQ(queue.getPendingCount(), 1u);

  queue.enqueue("http://example.com/b", DownloadQueue::Priority::High, [](HttpClient::Response) {});
  EXPECT_EQ(queue.getPendingCount(), 2u);
}

TEST_F(DownloadQueueTest, UpdateStartsDownloads) {
  DownloadQueue queue(client);
  client.setTimeout(5);

  queue.enqueue("http://invalid.invalid.invalid/a", DownloadQueue::Priority::Normal, [](HttpClient::Response) {});
  queue.enqueue("http://invalid.invalid.invalid/b", DownloadQueue::Priority::High, [](HttpClient::Response) {});

  queue.update();

  // After update, pending items should move to active.
  EXPECT_EQ(queue.getPendingCount(), 0u);
  EXPECT_EQ(queue.getActiveCount(), 2u);
}

TEST_F(DownloadQueueTest, CancelAll) {
  DownloadQueue queue(client);
  client.setTimeout(30);

  queue.enqueue("http://example.com/a", DownloadQueue::Priority::Low, [](HttpClient::Response) {});
  queue.enqueue("http://example.com/b", DownloadQueue::Priority::Normal, [](HttpClient::Response) {});

  queue.cancelAll();
  EXPECT_EQ(queue.getPendingCount(), 0u);
  EXPECT_EQ(queue.getActiveCount(), 0u);
}

TEST_F(DownloadQueueTest, CancelByUrl) {
  DownloadQueue queue(client);

  queue.enqueue("http://example.com/a", DownloadQueue::Priority::Normal, [](HttpClient::Response) {});
  queue.enqueue("http://example.com/b", DownloadQueue::Priority::Normal, [](HttpClient::Response) {});

  EXPECT_EQ(queue.getPendingCount(), 2u);

  queue.cancel("http://example.com/a");
  EXPECT_EQ(queue.getPendingCount(), 1u);
}

TEST_F(DownloadQueueTest, MaxConcurrentRespected) {
  DownloadQueue queue(client);
  client.setTimeout(30);
  queue.setMaxConcurrent(2);

  for (int i = 0; i < 5; ++i) {
    queue.enqueue("http://invalid.invalid.invalid/" + std::to_string(i),
                  DownloadQueue::Priority::Normal,
                  [](HttpClient::Response) {});
  }
  EXPECT_EQ(queue.getPendingCount(), 5u);

  queue.update();

  // Only 2 should be active, 3 still pending.
  EXPECT_EQ(queue.getActiveCount(), 2u);
  EXPECT_EQ(queue.getPendingCount(), 3u);
}

TEST_F(DownloadQueueTest, CompletedDownloadsFreeActiveSlots) {
  DownloadQueue queue(client);
  client.setTimeout(1);
  queue.setMaxConcurrent(1);

  std::atomic<int> completed{0};
  for (int i = 0; i < 3; ++i) {
    queue.enqueue("", DownloadQueue::Priority::Normal, [&completed](HttpClient::Response) { completed.fetch_add(1); });
  }

  queue.update();
  EXPECT_EQ(queue.getActiveCount(), 1u);
  EXPECT_EQ(queue.getPendingCount(), 2u);

  for (int i = 0; i < 100 && completed.load() < 3; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.update();
  }

  EXPECT_EQ(completed.load(), 3);
  EXPECT_EQ(queue.getActiveCount(), 0u);
  EXPECT_EQ(queue.getPendingCount(), 0u);
}
