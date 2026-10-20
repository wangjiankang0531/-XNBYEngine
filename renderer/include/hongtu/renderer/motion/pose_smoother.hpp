// renderer/include/hongtu/renderer/motion/pose_smoother.hpp
#pragma once

// 动态实体的显示位姿平滑(lane ⑫,spec docs/superpowers/specs/2026-10-19-motion-smooth-follow-design.md §3 / §4.1)。
//
// 应用侧按定位系统的节奏(典型 1–2 Hz)发来「新位姿」;本类把它变成逐帧连续的「显示位姿」:
//   - kNone  :落位(插值中来也立即停在新位姿);
//   - kFixed :从当前显示位姿匀速走到新位姿,用时 duration_s;插值中再来 → 从此刻的显示位姿重新起算;
//   - kAuto  :缓冲插值 —— 新位姿进队尾,每段用时 = 相邻两次命令间隔的估计(最近 auto_window 个间隔测值的中位数,夹到
//              [auto_min_s, auto_max_s];间隔 > auto_max_s 视为断流,不进估计;中位数而非均值:偶发的「两包挨着到」不把估计
//              拖垮);同一时刻(同帧,间隔 ≈ 0)到达的命令不计间隔(R2 M2:断流后一帧补发的一串包不把估计压到下限)。
//              积压剩余 > backlog_intervals 个间隔 → 加速回放(上限 max_catchup 倍)。段间不丢余量(否则每段首帧位移
//              偏小,1 Hz 下每秒一顿)。队列有界(R2 I2):排队 > kMaxQueued 段或剩余 > kMaxLagIntervals 个间隔 → 收拢 ——
//              丢掉中间航点,从当前显示位姿直奔最新目标(用时一个间隔估计),所以延迟上界 ≈ kMaxLagIntervals 个间隔;
//              remaining() O(1)(维护队列总时长)。
// 朝向只有偏航(度,绕竖轴):给了 yaw 的命令随位置按最短弧插值(绕固定轴的 slerp 即此);没给 yaw 且 face_motion → 朝向以
// 指数逼近(时间常数 face_tau_s)+ 限速(face_max_rate_deg_s)跟随显示速度方向(速度 < face_min_speed 不转)。
// 位置插值可注入(MotionLerpFn:geo 走弦 + 椭球高 + 逐点贴地,见 geo_motion.hpp);空 = 线性(local)。
// 纯数学、无引擎依赖、不加锁(调用方负责线程)。时间由调用方给(单调运动时钟,秒)。
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <functional>

namespace hongtu::renderer::motion {

  enum class SmoothMode {
    kNone,
    kFixed,
    kAuto
  };

  /// 一条命令的平滑参数(SDK element 的 smooth / face_motion / face_offset)。
  struct SmoothSpec {
    SmoothMode mode = SmoothMode::kNone;
    double duration_s = 0.0;      ///< kFixed 用时(秒,> 0)
    bool face_motion = false;     ///< 没给 yaw 时朝向跟随运动方向
    double face_offset_deg = 0.0; ///< 运动方位(场景系,自 +X 逆时针)之外再加的偏航(资产前向不是 +X 时用)
  };

  /// 场景系位姿。yaw_deg 自 +X 逆时针(度);ground_clamped = 该位置的高度是「贴地」(插值点逐点重新贴地)。
  struct MotionPose {
    glm::dvec3 pos{0.0};
    double yaw_deg = 0.0;
    bool ground_clamped = false;
  };

  /// 端点插值钩子:(a, b, s∈[0,1], a 贴地, b 贴地) → 位置。空 = 线性(贴地标记无效)。
  using MotionLerpFn =
      std::function<glm::dvec3(const glm::dvec3& a, const glm::dvec3& b, double s, bool a_ground, bool b_ground)>;

  struct MotionTuning {
    double auto_min_s = 0.05;           ///< auto 段时长下限
    double auto_max_s = 5.0;            ///< auto 段时长上限;更长的命令间隔视为断流,不进估计
    double auto_default_s = 1.0;        ///< 还没有任何间隔测值时的 auto 段时长
    double backlog_intervals = 1.5;     ///< 积压剩余超过这么多个间隔才加速
    double max_catchup = 4.0;           ///< 加速回放上限倍率
    double fixed_max_s = 60.0;          ///< kFixed 用时上限
    double face_min_speed = 0.1;        ///< 显示速度(水平,m/s)低于此不转向
    double face_tau_s = 0.2;            ///< 转向指数逼近时间常数
    double face_max_rate_deg_s = 360.0; ///< 转向限速
  };

  /// 最短弧角差 to − from,落在 [−180, 180]。
  inline double shortestArcDeg(double from_deg, double to_deg) {
    return std::remainder(to_deg - from_deg, 360.0);
  }

  inline glm::dvec3 linearMotionLerp(const glm::dvec3& a, const glm::dvec3& b, double s) {
    return a + (b - a) * s;
  }

  class PoseSmoother {
  public:
    explicit PoseSmoother(const MotionTuning& t = MotionTuning{}) : tune_(t) {
    }

    /// 落位并清空插值(实体新建 / 首次进入平滑)。now_s 记作「上次命令时刻」(auto 的首个间隔从这里量)。
    void reset(const MotionPose& p, double now_s) {
      from_ = p;
      shown_ = p;
      queue_.clear();
      seg_el_ = 0.0;
      vel_ = glm::dvec3(0.0);
      last_cmd_s_ = now_s;
      has_cmd_ = true;
    }

    /// 落位但不记命令时刻(该实体此前没有任何命令记录:下一次 auto 的间隔不可测,用 auto_default_s)。
    void reset(const MotionPose& p) {
      reset(p, 0.0);
      has_cmd_ = false;
    }

    /// 新目标到达。has_yaw = 命令给了朝向(否则保持 / face_motion)。target.ground_clamped = 该端点贴地。
    void push(const MotionPose& target, bool has_yaw, const SmoothSpec& spec, double now_s) {
      const double raw = has_cmd_ ? now_s - last_cmd_s_ : -1.0;
      last_cmd_s_ = now_s;
      has_cmd_ = true;
      face_motion_ = spec.face_motion;
      face_offset_deg_ = spec.face_offset_deg;
      if (spec.mode == SmoothMode::kAuto && raw > kSameFrameEps && raw <= tune_.auto_max_s) {
        win_[win_next_] = raw;
        win_next_ = (win_next_ + 1) % kAutoWindow;
        win_n_ = std::min(win_n_ + 1, kAutoWindow);
        std::array<double, kAutoWindow> v{};
        std::copy(win_.begin(), win_.begin() + static_cast<std::ptrdiff_t>(win_n_), v.begin());
        std::sort(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(win_n_));
        const double med = (win_n_ % 2 == 1) ? v[win_n_ / 2] : 0.5 * (v[win_n_ / 2 - 1] + v[win_n_ / 2]);
        est_s_ = std::clamp(med, tune_.auto_min_s, tune_.auto_max_s);
        has_est_ = true;
      }
      Waypoint w{target, has_yaw, 0.0, spec.mode == SmoothMode::kAuto};
      if (spec.mode == SmoothMode::kNone || (spec.mode == SmoothMode::kFixed && !(spec.duration_s > 0.0))) {
        queue_.clear();
        total_dur_ = 0.0;
        from_ = target;
        if (!has_yaw)
          from_.yaw_deg = shown_.yaw_deg;
        shown_ = from_;
        seg_el_ = 0.0;
        vel_ = glm::dvec3(0.0);
        return;
      }
      if (spec.mode == SmoothMode::kFixed) {
        w.dur = std::min(spec.duration_s, tune_.fixed_max_s);
        from_ = shown_;
        queue_.clear();
        queue_.push_back(w);
        total_dur_ = w.dur;
        seg_el_ = 0.0;
        return;
      }
      w.dur = has_est_ ? est_s_ : std::clamp(tune_.auto_default_s, tune_.auto_min_s, tune_.auto_max_s);
      if (queue_.empty()) {
        from_ = shown_;
        seg_el_ = 0.0;
        total_dur_ = 0.0;
      }
      queue_.push_back(w);
      total_dur_ += w.dur;
      // 有界(R2 I2):积压过多 → 丢中间航点,从当前显示位姿直奔最新目标,用时一个间隔估计。
      const double est = intervalEstimate();
      if (queue_.size() > kMaxQueued || remaining() > kMaxLagIntervals * est + kEps) {
        Waypoint last = queue_.back();
        last.dur = est;
        from_ = shown_;
        queue_.clear();
        queue_.push_back(last);
        total_dur_ = last.dur;
        seg_el_ = 0.0;
        ++collapses_;
      }
    }

    /// 推进 dt 秒。返回本步显示位姿是否变化。dt ≤ 0 不动(离线 geo 瓦片放行前)。
    bool step(double dt, const MotionLerpFn& lerp = MotionLerpFn{}) {
      if (!(dt > 0.0))
        return false;
      const MotionPose before = shown_;
      // auto 积压加速:剩余时长超过 backlog_intervals 个间隔时按比例加速(只看队首是 auto 段)。
      double rate = 1.0;
      if (!queue_.empty() && queue_.front().is_auto && has_est_) {
        const double lag = tune_.backlog_intervals * est_s_;
        const double rem = remaining();
        if (lag > 0.0 && rem > lag)
          rate = std::min(tune_.max_catchup, rem / lag);
      }
      double adv = dt * rate;
      bool yaw_driven = false; // 本步朝向由显式 yaw 段决定
      while (!queue_.empty()) {
        const Waypoint& w = queue_.front();
        const double left = w.dur - seg_el_;
        if (adv < left - kEps) { // kEps:累加 dt 的舍入不该让一段多拖一帧
          seg_el_ += adv;
          break;
        }
        adv -= left;
        // 走完这一段:段终点成为下一段起点(余量 adv 接着走下一段,不丢)。
        from_.pos = w.pose.pos;
        from_.ground_clamped = w.pose.ground_clamped;
        if (w.has_yaw) {
          from_.yaw_deg = w.pose.yaw_deg;
          yaw_driven = true;
        } else {
          from_.yaw_deg = shown_.yaw_deg;
        }
        total_dur_ -= w.dur;
        queue_.pop_front();
        seg_el_ = 0.0;
        if (queue_.empty())
          total_dur_ = 0.0;
      }
      if (queue_.empty()) {
        shown_.pos = from_.pos;
        shown_.ground_clamped = from_.ground_clamped;
        if (yaw_driven)
          shown_.yaw_deg = from_.yaw_deg;
      } else {
        const Waypoint& w = queue_.front();
        const double s = w.dur > 0.0 ? std::clamp(seg_el_ / w.dur, 0.0, 1.0) : 1.0;
        shown_.pos = lerp ? lerp(from_.pos, w.pose.pos, s, from_.ground_clamped, w.pose.ground_clamped)
                          : linearMotionLerp(from_.pos, w.pose.pos, s);
        shown_.ground_clamped = w.pose.ground_clamped;
        if (w.has_yaw) {
          shown_.yaw_deg = from_.yaw_deg + shortestArcDeg(from_.yaw_deg, w.pose.yaw_deg) * s;
          yaw_driven = true;
        }
      }
      vel_ = (shown_.pos - before.pos) / dt;
      if (!yaw_driven && face_motion_) {
        const double spd = speed();
        if (spd >= tune_.face_min_speed) {
          const double want = std::atan2(vel_.y, vel_.x) * 57.29577951308232 + face_offset_deg_;
          const double diff = shortestArcDeg(shown_.yaw_deg, want);
          double d = diff * (1.0 - std::exp(-dt / std::max(tune_.face_tau_s, 1e-6)));
          const double cap = tune_.face_max_rate_deg_s * dt;
          d = std::clamp(d, -cap, cap);
          shown_.yaw_deg = std::remainder(shown_.yaw_deg + d, 360.0);
          if (queue_.empty())
            from_.yaw_deg = shown_.yaw_deg;
        }
      }
      if (queue_.empty() && !yaw_driven)
        from_.yaw_deg = shown_.yaw_deg;
      return shown_.pos != before.pos || shown_.yaw_deg != before.yaw_deg;
    }

    const MotionPose& displayed() const {
      return shown_;
    }
    /// 最近一步的显示速度(场景系 m/s)。
    glm::dvec3 velocity() const {
      return vel_;
    }
    /// 水平显示速度(m/s)—— 骨骼人物 idle / walk / run 判定、朝向跟随用。
    double speed() const {
      return std::sqrt(vel_.x * vel_.x + vel_.y * vel_.y);
    }
    /// 还在插值(队列非空)。
    bool active() const {
      return !queue_.empty();
    }
    /// auto 段时长估计(没有测值时为 auto_default_s)。
    double intervalEstimate() const {
      return has_est_ ? est_s_ : tune_.auto_default_s;
    }
    /// 队里剩余的插值时长(秒,未加速)。O(1):维护的队列总时长 − 当前段已走时长。
    double remaining() const {
      return queue_.empty() ? 0.0 : std::max(0.0, total_dur_ - seg_el_);
    }
    /// 收拢次数(积压超界丢中间航点的次数;取证用)。
    std::size_t collapses() const {
      return collapses_;
    }
    std::size_t queued() const {
      return queue_.size();
    }
    /// 逐段求和的剩余时长(单测核对 remaining() 的 O(1) 维护用)。
    double remainingSlowForTest() const {
      double r = 0.0;
      bool first = true;
      for (const auto& w : queue_) {
        r += first ? std::max(0.0, w.dur - seg_el_) : w.dur;
        first = false;
      }
      return r;
    }
    /// auto 队列上界(R2 I2):排队段数 / 剩余时长(以间隔估计计)超过即收拢。
    static constexpr std::size_t kMaxQueued = 8;
    static constexpr double kMaxLagIntervals = 3.0;
    /// 当前插值终点(队尾;空队列 = 显示位姿)。队尾没给朝向(保持 / face_motion)时朝向取当前显示朝向。
    MotionPose finalTarget() const {
      if (queue_.empty())
        return shown_;
      MotionPose t = queue_.back().pose;
      if (!queue_.back().has_yaw)
        t.yaw_deg = shown_.yaw_deg;
      return t;
    }
    /// 队尾目标是否给了朝向(没给 = 朝向由保持 / face_motion 决定)。
    bool finalHasYaw() const {
      return !queue_.empty() && queue_.back().has_yaw;
    }

  private:
    static constexpr std::size_t kAutoWindow = 5; ///< auto 间隔估计窗口(中位数)
    static constexpr double kEps = 1e-9;
    static constexpr double kSameFrameEps = 1e-4; ///< 间隔 ≤ 此值 = 同帧到达,不计入间隔估计(R2 M2)
    struct Waypoint {
      MotionPose pose;
      bool has_yaw = false;
      double dur = 0.0;
      bool is_auto = false;
    };
    MotionTuning tune_;
    MotionPose from_{};
    MotionPose shown_{};
    std::deque<Waypoint> queue_;
    double seg_el_ = 0.0;
    double total_dur_ = 0.0; ///< 队列各段时长之和(remaining() O(1))
    std::size_t collapses_ = 0;
    glm::dvec3 vel_{0.0};
    double last_cmd_s_ = 0.0;
    bool has_cmd_ = false;
    double est_s_ = 1.0;
    bool has_est_ = false;
    std::array<double, kAutoWindow> win_{};
    std::size_t win_n_ = 0, win_next_ = 0;
    bool face_motion_ = false;
    double face_offset_deg_ = 0.0;
  };

} // namespace hongtu::renderer::motion
