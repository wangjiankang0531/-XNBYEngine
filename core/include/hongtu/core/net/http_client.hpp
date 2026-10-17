#pragma once

#include <hongtu/core/common.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hongtu::core::net {

  /// HTTP client wrapping libcurl with sync and async support.
  class HttpClient {
  public:
    struct Response {
      i32 status_code = 0;
      std::vector<u8> body;
      std::string error;
      bool transfer_completed = false;

      /// HTTP(S) succeeds on 2xx. Non-HTTP curl transports such as file:// have
      /// no response code (0), so use the explicit transport completion bit.
      bool success() const {
        return transfer_completed && error.empty() && (status_code == 0 || (status_code >= 200 && status_code < 300));
      }
    };

    using Callback = std::function<void(Response)>;
    using RequestId = u64;

    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    /// Perform a synchronous HTTP GET request.
    Response get(const std::string& url);

    /// Perform an asynchronous HTTP GET request. Returns a request ID.
    RequestId getAsync(const std::string& url, Callback callback);

    /// Cancel a pending async request by ID.
    void cancelRequest(RequestId id);

    /// Dispatch completed async callbacks on the calling thread.
    void update();

    /// Set the request timeout in seconds (default: 30).
    void setTimeout(i32 seconds);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace hongtu::core::net
