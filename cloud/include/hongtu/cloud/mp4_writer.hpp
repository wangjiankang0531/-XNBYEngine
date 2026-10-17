#pragma once

#include <hongtu/core/common.hpp>

#include <cstdint>
#include <string>

namespace hongtu::cloud {

/// H.264 .mp4 文件写出(AVAssetWriter,macOS only)。
///
/// 离线产出管线(园区绕场视频导出)用:逐帧喂 BGRA8,内部硬编 H.264 并
/// 封装 mp4 —— 与 VTEncoder 的区别是这里要的是"文件",编码器+muxer 一体,
/// 不需要 Annex-B/RTP 那套流式约定。
class Mp4Writer {
 public:
  struct Config {
    u32 width        = 1920;
    u32 height       = 1080;
    f32 fps          = 30.0f;
    u32 bitrate_kbps = 12000;  ///< 离线档默认拉高,清晰度优先
  };

  Mp4Writer() = default;
  ~Mp4Writer();

  Mp4Writer(const Mp4Writer&) = delete;
  Mp4Writer& operator=(const Mp4Writer&) = delete;

  /// 目标文件已存在则覆盖。
  bool open(const std::string& path, const Config& cfg);

  /// 追加一帧(stride = width*4)。`pts_seconds` 单调递增。
  bool appendBGRA(const uint8_t* bgra, double pts_seconds);

  /// 收尾写 moov;成功后文件可播。析构时未 finalize 会丢弃文件尾。
  bool finalize();

 private:
  struct Impl;
  Impl*  impl_ = nullptr;
  Config cfg_{};
};

}  // namespace hongtu::cloud
