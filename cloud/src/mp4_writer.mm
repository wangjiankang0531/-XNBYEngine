// Mp4Writer — AVAssetWriter 实现(硬编 H.264 + mp4 封装一体)。
#ifdef __APPLE__

#include <hongtu/cloud/mp4_writer.hpp>

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

#include <spdlog/spdlog.h>

#include <cstring>

namespace hongtu::cloud {

struct Mp4Writer::Impl {
  AVAssetWriter*                          writer  = nil;
  AVAssetWriterInput*                     input   = nil;
  AVAssetWriterInputPixelBufferAdaptor*   adaptor = nil;
};

Mp4Writer::~Mp4Writer() {
  if (impl_) {
    if (impl_->writer && impl_->writer.status == AVAssetWriterStatusWriting) {
      [impl_->writer cancelWriting];
    }
    delete impl_;
  }
}

bool Mp4Writer::open(const std::string& path, const Config& cfg) {
  cfg_  = cfg;
  impl_ = new Impl();

  NSString* nspath = [NSString stringWithUTF8String:path.c_str()];
  [[NSFileManager defaultManager] removeItemAtPath:nspath error:nil];
  NSURL* url = [NSURL fileURLWithPath:nspath];

  NSError* err = nil;
  impl_->writer = [[AVAssetWriter alloc] initWithURL:url
                                            fileType:AVFileTypeMPEG4
                                               error:&err];
  if (!impl_->writer || err) {
    spdlog::error("Mp4Writer: AVAssetWriter init failed: {}",
                  err ? err.localizedDescription.UTF8String : "?");
    return false;
  }

  NSDictionary* settings = @{
    AVVideoCodecKey  : AVVideoCodecTypeH264,
    AVVideoWidthKey  : @(cfg.width),
    AVVideoHeightKey : @(cfg.height),
    AVVideoCompressionPropertiesKey : @{
      AVVideoAverageBitRateKey : @(cfg.bitrate_kbps * 1000),
      AVVideoMaxKeyFrameIntervalKey : @(static_cast<int>(cfg.fps * 2)),
      AVVideoProfileLevelKey : AVVideoProfileLevelH264HighAutoLevel,
    },
  };
  impl_->input = [[AVAssetWriterInput alloc]
      initWithMediaType:AVMediaTypeVideo
         outputSettings:settings];
  impl_->input.expectsMediaDataInRealTime = NO;

  NSDictionary* pbAttrs = @{
    (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
    (id)kCVPixelBufferWidthKey  : @(cfg.width),
    (id)kCVPixelBufferHeightKey : @(cfg.height),
  };
  impl_->adaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc]
      initWithAssetWriterInput:impl_->input
   sourcePixelBufferAttributes:pbAttrs];

  if (![impl_->writer canAddInput:impl_->input]) {
    spdlog::error("Mp4Writer: cannot add video input");
    return false;
  }
  [impl_->writer addInput:impl_->input];
  if (![impl_->writer startWriting]) {
    spdlog::error("Mp4Writer: startWriting failed ({})",
                  impl_->writer.error
                      ? impl_->writer.error.localizedDescription.UTF8String
                      : "?");
    return false;
  }
  [impl_->writer startSessionAtSourceTime:kCMTimeZero];
  spdlog::info("Mp4Writer: {} {}x{} @{}fps {}kbps", path, cfg.width, cfg.height,
               cfg.fps, cfg.bitrate_kbps);
  return true;
}

bool Mp4Writer::appendBGRA(const uint8_t* bgra, double pts_seconds) {
  if (!impl_ || !impl_->adaptor || !bgra) return false;

  // 离线编码:写入口忙时自旋等待(expectsMediaDataInRealTime=NO 合法)。
  while (!impl_->input.readyForMoreMediaData) {
    usleep(2000);
    if (impl_->writer.status != AVAssetWriterStatusWriting) return false;
  }

  CVPixelBufferRef pixbuf = nullptr;
  CVPixelBufferPoolRef pool = impl_->adaptor.pixelBufferPool;
  if (!pool ||
      CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &pixbuf) !=
          kCVReturnSuccess) {
    return false;
  }
  CVPixelBufferLockBaseAddress(pixbuf, 0);
  uint8_t* dst = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixbuf));
  const size_t dst_stride = CVPixelBufferGetBytesPerRow(pixbuf);
  const size_t src_stride = static_cast<size_t>(cfg_.width) * 4;
  for (u32 row = 0; row < cfg_.height; ++row) {
    std::memcpy(dst + row * dst_stride, bgra + row * src_stride, src_stride);
  }
  CVPixelBufferUnlockBaseAddress(pixbuf, 0);

  const CMTime pts = CMTimeMakeWithSeconds(pts_seconds, 90000);
  const BOOL ok = [impl_->adaptor appendPixelBuffer:pixbuf
                               withPresentationTime:pts];
  CVPixelBufferRelease(pixbuf);
  if (!ok) {
    spdlog::error("Mp4Writer: append failed ({})",
                  impl_->writer.error
                      ? impl_->writer.error.localizedDescription.UTF8String
                      : "?");
  }
  return ok;
}

bool Mp4Writer::finalize() {
  if (!impl_ || !impl_->writer) return false;
  [impl_->input markAsFinished];
  __block bool done = false;
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);
  [impl_->writer finishWritingWithCompletionHandler:^{
    done = true;
    dispatch_semaphore_signal(sem);
  }];
  dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 60 * NSEC_PER_SEC));
  const bool ok = done && impl_->writer.status == AVAssetWriterStatusCompleted;
  if (!ok) {
    spdlog::error("Mp4Writer: finalize failed (status={})",
                  static_cast<long>(impl_->writer.status));
  }
  return ok;
}

}  // namespace hongtu::cloud

#endif  // __APPLE__
