// Minimal blocking HTTP/1.1 server for cloud-render signaling.
//
// Two routes are all WebRTC needs here: GET / serves the player page and
// POST /offer exchanges SDP. Deliberately tiny — no keep-alive, no chunked
// encoding, one connection handled at a time on a dedicated thread. This is
// app-level plumbing; networking stays OUT of the engine libraries.
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

namespace crs {

class HttpServer {
 public:
  /// (method, path, body) → (status, content_type, response_body)
  struct Response {
    int         status = 200;
    std::string content_type = "text/plain";
    std::string body;
  };
  using Handler = std::function<Response(const std::string& method,
                                         const std::string& path,
                                         const std::string& body)>;

  ~HttpServer() { stop(); }

  /// bind_addr 解析失败时回退 0.0.0.0;agent 托管实例时传 127.0.0.1,
  /// 信令只对同机 agent 可见(媒体流走 WebRTC 自己的 UDP,不受影响)。
  bool start(uint16_t port, Handler handler, const std::string& bind_addr = "0.0.0.0") {
    handler_ = std::move(handler);
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    int yes = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    if (::inet_pton(AF_INET, bind_addr.c_str(), &addr.sin_addr) != 1)
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(fd_, 8) != 0) {
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    running_ = true;
    thread_  = std::thread([this] { acceptLoop(); });
    return true;
  }

  void stop() {
    running_ = false;
    if (fd_ >= 0) {
      ::shutdown(fd_, SHUT_RDWR);
      ::close(fd_);
      fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
  }

 private:
  void acceptLoop() {
    while (running_) {
      int client = ::accept(fd_, nullptr, nullptr);
      if (client < 0) break;
      handleClient(client);
      ::close(client);
    }
  }

  void handleClient(int client) {
    std::string req;
    char buf[4096];
    size_t header_end = std::string::npos;
    // Read until end of headers.
    while (header_end == std::string::npos) {
      ssize_t n = ::recv(client, buf, sizeof(buf), 0);
      if (n <= 0) return;
      req.append(buf, static_cast<size_t>(n));
      header_end = req.find("\r\n\r\n");
      if (req.size() > 1 << 20) return;  // sanity cap
    }
    // Parse request line.
    const size_t sp1 = req.find(' ');
    const size_t sp2 = req.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return;
    const std::string method = req.substr(0, sp1);
    const std::string path   = req.substr(sp1 + 1, sp2 - sp1 - 1);
    // Body via Content-Length.
    size_t content_len = 0;
    {
      std::string lower = req.substr(0, header_end);
      for (auto& c : lower) c = static_cast<char>(::tolower(c));
      const size_t k = lower.find("content-length:");
      if (k != std::string::npos) content_len = std::strtoul(lower.c_str() + k + 15, nullptr, 10);
    }
    std::string body = req.substr(header_end + 4);
    while (body.size() < content_len) {
      ssize_t n = ::recv(client, buf, sizeof(buf), 0);
      if (n <= 0) break;
      body.append(buf, static_cast<size_t>(n));
    }

    Response r = handler_ ? handler_(method, path, body) : Response{404, "text/plain", "no handler"};
    const char* status_text = (r.status == 200) ? "OK" : (r.status == 404 ? "Not Found" : "Error");
    std::string out = "HTTP/1.1 " + std::to_string(r.status) + " " + status_text +
                      "\r\nContent-Type: " + r.content_type +
                      "\r\nContent-Length: " + std::to_string(r.body.size()) +
                      "\r\nAccess-Control-Allow-Origin: *"
                      "\r\nConnection: close\r\n\r\n" + r.body;
    size_t sent = 0;
    while (sent < out.size()) {
      ssize_t n = ::send(client, out.data() + sent, out.size() - sent, 0);
      if (n <= 0) break;
      sent += static_cast<size_t>(n);
    }
  }

  int               fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread       thread_;
  Handler           handler_;
};

}  // namespace crs
