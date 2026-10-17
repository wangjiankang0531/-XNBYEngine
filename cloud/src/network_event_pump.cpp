#include <hongtu/cloud/network_event_pump.hpp>

namespace hongtu::cloud {

bool NetworkEventPump::enqueue(hongtu::window::InputEvent ev) {
  std::lock_guard<std::mutex> lk(mtx_);
  queue_.push_back(std::move(ev));
  return true;
}

bool NetworkEventPump::poll(hongtu::window::InputEvent& out) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (queue_.empty()) return false;
  out = std::move(queue_.front());
  queue_.pop_front();
  return true;
}

std::size_t NetworkEventPump::pending() const {
  std::lock_guard<std::mutex> lk(mtx_);
  return queue_.size();
}

void NetworkEventPump::clear() {
  std::lock_guard<std::mutex> lk(mtx_);
  queue_.clear();
}

}  // namespace hongtu::cloud
