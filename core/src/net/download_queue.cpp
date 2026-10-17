#include <hongtu/core/net/download_queue.hpp>

#include <algorithm>
#include <queue>
#include <vector>

namespace hongtu::core::net {

struct DownloadQueue::Impl : std::enable_shared_from_this<DownloadQueue::Impl> {
  HttpClient& client;
  i32 max_concurrent{6};

  struct PendingItem {
    std::string url;
    Priority priority;
    Callback callback;
    u64 sequence;  // for stable ordering within same priority

    bool operator<(const PendingItem& other) const {
      if (priority != other.priority) {
        return static_cast<i32>(priority) < static_cast<i32>(other.priority);
      }
      // Higher sequence = added later = lower priority in same tier
      return sequence > other.sequence;
    }
  };

  struct ActiveItem {
    std::string url;
    HttpClient::RequestId request_id;
  };

  std::priority_queue<PendingItem> pending;
  std::vector<ActiveItem> active;
  u64 next_sequence{0};

  explicit Impl(HttpClient& c) : client(c) {}

  void startDownloads() {
    while (static_cast<i32>(active.size()) < max_concurrent && !pending.empty()) {
      auto item = pending.top();
      pending.pop();

      auto cb = std::move(item.callback);
      auto url = item.url;
      auto request_id_holder = std::make_shared<HttpClient::RequestId>(0);
      std::weak_ptr<Impl> weak_impl = weak_from_this();

      auto request_id = client.getAsync(url, [weak_impl, request_id_holder, cb = std::move(cb)](
                                                 HttpClient::Response response) mutable {
        auto impl = weak_impl.lock();
        if (!impl) {
          return;
        }

        const auto request_id = *request_id_holder;
        auto it = std::find_if(impl->active.begin(), impl->active.end(),
                               [request_id](const ActiveItem& active) {
                                 return active.request_id == request_id;
                               });
        if (it == impl->active.end()) {
          return;
        }

        impl->active.erase(it);
        if (cb) {
          cb(std::move(response));
        }
      });

      *request_id_holder = request_id;
      active.push_back({std::move(url), request_id});
    }
  }
};

DownloadQueue::DownloadQueue(HttpClient& client) : impl_(std::make_shared<Impl>(client)) {}

DownloadQueue::~DownloadQueue() {
  cancelAll();
}

void DownloadQueue::enqueue(const std::string& url, Priority priority, Callback callback) {
  impl_->pending.push({url, priority, std::move(callback), impl_->next_sequence++});
}

void DownloadQueue::cancel(const std::string& url) {
  // Remove from active
  auto& active = impl_->active;
  for (auto it = active.begin(); it != active.end(); ++it) {
    if (it->url == url) {
      impl_->client.cancelRequest(it->request_id);
      active.erase(it);
      break;
    }
  }

  // Rebuild pending queue without the cancelled URL
  std::priority_queue<Impl::PendingItem> new_pending;
  while (!impl_->pending.empty()) {
    auto item = impl_->pending.top();
    impl_->pending.pop();
    if (item.url != url) {
      new_pending.push(std::move(item));
    }
  }
  impl_->pending = std::move(new_pending);
}

void DownloadQueue::cancelAll() {
  for (auto& item : impl_->active) {
    impl_->client.cancelRequest(item.request_id);
  }
  impl_->active.clear();

  while (!impl_->pending.empty()) {
    impl_->pending.pop();
  }
}

void DownloadQueue::update() {
  impl_->client.update();
  impl_->startDownloads();
}

void DownloadQueue::setMaxConcurrent(i32 n) {
  impl_->max_concurrent = n;
}

size_t DownloadQueue::getPendingCount() const {
  return impl_->pending.size();
}

size_t DownloadQueue::getActiveCount() const {
  return impl_->active.size();
}

}  // namespace hongtu::core::net
