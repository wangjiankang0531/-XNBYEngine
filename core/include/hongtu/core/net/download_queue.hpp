#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/core/net/http_client.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hongtu::core::net {

/// Prioritized concurrent download queue built on top of HttpClient.
class DownloadQueue {
 public:
  enum class Priority : i32 { Low = 0, Normal = 1, High = 2, Critical = 3 };

  using Callback = std::function<void(HttpClient::Response response)>;

  explicit DownloadQueue(HttpClient& client);
  ~DownloadQueue();

  DownloadQueue(const DownloadQueue&) = delete;
  DownloadQueue& operator=(const DownloadQueue&) = delete;

  /// Enqueue a URL for download with the given priority.
  void enqueue(const std::string& url, Priority priority, Callback callback);

  /// Cancel a pending download by URL.
  void cancel(const std::string& url);

  /// Cancel all pending and active downloads.
  void cancelAll();

  /// Start new downloads up to max concurrent and dispatch completed results.
  void update();

  /// Set the maximum number of concurrent downloads (default: 6).
  void setMaxConcurrent(i32 n);

  /// Number of queued (not yet started) downloads.
  size_t getPendingCount() const;

  /// Number of currently active (in-flight) downloads.
  size_t getActiveCount() const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace hongtu::core::net
