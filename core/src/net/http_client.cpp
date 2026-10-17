#include <curl/curl.h>
#include <hongtu/core/net/http_client.hpp>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace hongtu::core::net {

  namespace {

    size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
      auto* buffer = static_cast<std::vector<u8>*>(userp);
      auto total = size * nmemb;
      auto* data = static_cast<const u8*>(contents);
      buffer->insert(buffer->end(), data, data + total);
      return total;
    }

    int progressCallback(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
      auto* cancelled = static_cast<std::atomic<bool>*>(clientp);
      return cancelled && cancelled->load() ? 1 : 0;
    }

    /// Locate a CA certificate bundle. vcpkg's libcurl uses OpenSSL which has no
    /// built-in trust store on macOS — without an explicit CAINFO it returns
    /// "SSL connect error" for every HTTPS request.
    const char* findCABundle() {
      static const std::string path = []() -> std::string {
        if (const char* env = std::getenv("CURL_CA_BUNDLE"); env && *env)
          return env;
        if (const char* env = std::getenv("SSL_CERT_FILE"); env && *env)
          return env;
        for (const char* candidate : {
                 "/opt/homebrew/etc/ca-certificates/cert.pem", // Apple Silicon Homebrew
                 "/opt/homebrew/etc/openssl@3/cert.pem",
                 "/usr/local/etc/ca-certificates/cert.pem", // Intel Homebrew
                 "/usr/local/etc/openssl@3/cert.pem",
                 "/etc/ssl/cert.pem",                  // macOS default
                 "/etc/ssl/certs/ca-certificates.crt", // Linux
             }) {
          std::error_code ec;
          if (std::filesystem::exists(candidate, ec))
            return candidate;
        }
        return {};
      }();
      return path.empty() ? nullptr : path.c_str();
    }

  } // namespace

  struct HttpClient::Impl {
    std::atomic<u64> next_id{1};
    std::atomic<bool> shutting_down{false};
    i32 timeout_seconds{60}; // tiles over slow paths can take >30s

    // Async infrastructure
    struct CompletedRequest {
      Callback callback;
      Response response;
    };

    std::mutex mutex;
    std::vector<CompletedRequest> completed;
    std::unordered_map<u64, std::atomic<bool>*> cancel_flags;
    std::mutex cancel_mutex;
    std::vector<std::thread> workers;
    std::mutex workers_mutex;

    Response performGet(const std::string& url, std::atomic<bool>* cancelled = nullptr) {
      Response response;

      CURL* curl = curl_easy_init();
      if (!curl) {
        response.error = "Failed to initialize curl";
        return response;
      }

      std::vector<u8> buffer;
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
      curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
      // Treat very slow responses as failure so retry happens sooner instead of
      // sitting idle: less than 1 KB/s for 20s = stall.
      curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
      curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
      if (cancelled) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancelled);
      }
      curl_easy_setopt(curl,
                       CURLOPT_USERAGENT,
                       "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                       "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36");
      curl_easy_setopt(curl, CURLOPT_REFERER, "https://www.tianditu.gov.cn/");

      // OpenSSL backend has no built-in trust store on macOS — point libcurl
      // at a CA bundle so HTTPS handshakes don't fail with "SSL connect error".
      if (const char* ca = findCABundle()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
      }

      // Force IPv4. Many Chinese imagery hosts (Tianditu, etc.) advertise
      // AAAA records but the IPv6 endpoint either drops the TLS handshake
      // mid-stream or routes poorly from outside; OpenSSL 3 then reports
      // "TLS connect error: unexpected eof while reading". IPv4 routes
      // through CDN nodes that work reliably.
      curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

      struct curl_slist* headers = nullptr;
      headers = curl_slist_append(headers,
                                  "Accept: image/avif,image/webp,image/apng,image/svg+xml,"
                                  "image/*,*/*;q=0.8");
      headers = curl_slist_append(headers, "Accept-Language: zh-CN,zh;q=0.9,en;q=0.8");
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

      CURLcode res = curl_easy_perform(curl);

      if (cancelled && cancelled->load()) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        response.error = "Request cancelled";
        return response;
      }

      if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
      } else {
        response.transfer_completed = true;
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        response.status_code = static_cast<i32>(http_code);
        response.body = std::move(buffer);
      }

      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);
      return response;
    }
  };

  HttpClient::HttpClient() : impl_(std::make_unique<Impl>()) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
  }

  HttpClient::~HttpClient() {
    impl_->shutting_down.store(true);

    {
      std::lock_guard<std::mutex> lock(impl_->cancel_mutex);
      for (auto& [id, cancelled] : impl_->cancel_flags) {
        HT_UNUSED(id);
        cancelled->store(true);
      }
    }

    std::vector<std::thread> workers;
    {
      std::lock_guard<std::mutex> lock(impl_->workers_mutex);
      workers.swap(impl_->workers);
    }

    for (auto& worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }

    curl_global_cleanup();
  }

  HttpClient::Response HttpClient::get(const std::string& url) {
    return impl_->performGet(url);
  }

  HttpClient::RequestId HttpClient::getAsync(const std::string& url, Callback callback) {
    u64 id = impl_->next_id.fetch_add(1);

    auto* cancelled = new std::atomic<bool>(false);
    {
      std::lock_guard<std::mutex> lock(impl_->cancel_mutex);
      impl_->cancel_flags[id] = cancelled;
    }

    auto* impl = impl_.get();
    std::thread worker([impl, url, callback = std::move(callback), cancelled, id]() {
      Response response = impl->performGet(url, cancelled);

      {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if (!cancelled->load() && !impl->shutting_down.load()) {
          impl->completed.push_back({callback, std::move(response)});
        }
      }

      {
        std::lock_guard<std::mutex> lock(impl->cancel_mutex);
        impl->cancel_flags.erase(id);
      }
      delete cancelled;
    });

    {
      std::lock_guard<std::mutex> lock(impl_->workers_mutex);
      impl_->workers.push_back(std::move(worker));
    }

    return id;
  }

  void HttpClient::cancelRequest(RequestId id) {
    std::lock_guard<std::mutex> lock(impl_->cancel_mutex);
    auto it = impl_->cancel_flags.find(id);
    if (it != impl_->cancel_flags.end()) {
      it->second->store(true);
    }
  }

  void HttpClient::update() {
    std::vector<Impl::CompletedRequest> to_dispatch;
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      to_dispatch.swap(impl_->completed);
    }

    for (auto& req : to_dispatch) {
      if (req.callback) {
        req.callback(std::move(req.response));
      }
    }
  }

  void HttpClient::setTimeout(i32 seconds) {
    impl_->timeout_seconds = seconds;
  }

} // namespace hongtu::core::net
