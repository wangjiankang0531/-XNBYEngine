#pragma once

#include <hongtu/scene/timeline/track.hpp>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace hongtu::scene {

/// Abstract sample-the-track-and-do-something interface. Per `feedback_engine_vs_demo`
/// + user's pick, driving Entity / Emitter via callback injection — Entity stays
/// ignorant of Timeline.
class TimelineDriver {
 public:
  virtual ~TimelineDriver() = default;
  virtual void sample(float t) = 0;
};

/// Generic callback driver — pairs a Track<T> with a setter that receives the
/// sampled value. Used for all 4 stock driver types described in the spec
/// (EntityTransform / EmitterEnable / CameraPath / AnnotationVisibility) by
/// just supplying the right T + lambda.
template <typename T>
class CallbackDriver : public TimelineDriver {
 public:
  CallbackDriver(Track<T> track, std::function<void(const T&)> setter)
      : track_(std::move(track)), setter_(std::move(setter)) {}

  void sample(float t) override {
    if (!track_.empty() && setter_) setter_(track_.sample(t));
  }

  const Track<T>& track() const { return track_; }

 private:
  Track<T> track_;
  std::function<void(const T&)> setter_;
};

/// Playback head over a set of drivers. Each `update(dt)` advances internal
/// time (when playing), then samples every driver at the current time.
class Timeline {
 public:
  void addDriver(std::unique_ptr<TimelineDriver> d) {
    drivers_.push_back(std::move(d));
  }
  void clearDrivers() { drivers_.clear(); }
  size_t driverCount() const { return drivers_.size(); }

  void  setDuration(float seconds) { duration_ = seconds; }
  float getDuration() const        { return duration_; }

  void  play()        { playing_ = true; }
  void  pause()       { playing_ = false; }
  void  stop()        { playing_ = false; time_ = 0.0f; }
  bool  isPlaying() const { return playing_; }

  void  seek(float t) { time_ = t; }
  float currentTime() const { return time_; }

  void  setSpeed(float s) { speed_ = s; }
  float getSpeed() const  { return speed_; }

  void  setLoop(bool on) { loop_ = on; }
  bool  getLoop() const  { return loop_; }

  /// Advance time (if playing) + sample all drivers. Caller passes wall-clock
  /// dt. Drivers may apply side effects (e.g. mutate Entity position).
  void update(float dt) {
    if (playing_) {
      time_ += dt * speed_;
      if (loop_ && duration_ > 0.0f) {
        if (time_ > duration_) {
          time_ = std::fmod(time_, duration_);
          if (time_ < 0.0f) time_ += duration_;
        }
      } else if (time_ > duration_) {
        time_    = duration_;
        playing_ = false;
      }
    }
    for (auto& d : drivers_) d->sample(time_);
  }

 private:
  std::vector<std::unique_ptr<TimelineDriver>> drivers_;
  float time_     = 0.0f;
  float duration_ = 10.0f;
  float speed_    = 1.0f;
  bool  playing_  = false;
  bool  loop_     = false;
};

}  // namespace hongtu::scene
