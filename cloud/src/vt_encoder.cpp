#ifdef __APPLE__

#include <hongtu/cloud/vt_encoder.hpp>

#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

#include <spdlog/spdlog.h>

#include <cstring>

namespace hongtu::cloud {

struct VTEncoder::Impl {
  VTCompressionSessionRef session = nullptr;
};

// AVCC sample buffer → Annex-B, prepending SPS/PPS on keyframes.
namespace {

void appendStartCode(std::vector<uint8_t>& out) {
  out.insert(out.end(), {0x00, 0x00, 0x00, 0x01});
}

bool isKeyframe(CMSampleBufferRef sample) {
  CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
  if (!attachments || CFArrayGetCount(attachments) == 0) return true;
  CFDictionaryRef dict =
      (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
  return !CFDictionaryContainsKey(dict, kCMSampleAttachmentKey_NotSync);
}

void sampleToAnnexB(CMSampleBufferRef sample, bool keyframe,
                    std::vector<uint8_t>& out) {
  out.clear();

  if (keyframe) {
    CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(sample);
    size_t count = 0;
    CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 0, nullptr,
                                                       nullptr, &count, nullptr);
    for (size_t i = 0; i < count; ++i) {
      const uint8_t* ps = nullptr;
      size_t ps_size = 0;
      if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
              fmt, i, &ps, &ps_size, nullptr, nullptr) == noErr) {
        appendStartCode(out);
        out.insert(out.end(), ps, ps + ps_size);
      }
    }
  }

  CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
  const size_t total = CMBlockBufferGetDataLength(block);
  size_t contiguous = 0;
  char*  data = nullptr;
  if (CMBlockBufferGetDataPointer(block, 0, &contiguous, nullptr, &data) != noErr) {
    return;
  }
  // 关键:GetDataPointer 的指针只覆盖第一个连续区段。大帧的 block buffer
  // 可能分段 —— 按 total 直读会把越界垃圾混进码流,解码端闪黑块。
  static thread_local std::vector<uint8_t> linear;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
  if (contiguous < total) {
    linear.resize(total);
    if (CMBlockBufferCopyDataBytes(block, 0, total, linear.data()) != noErr) return;
    p = linear.data();
  }
  // AVCC: [4-byte BE length][NAL] ... → start-code framing.
  size_t off = 0;
  while (off + 4 <= total) {
    uint32_t nal_len = (uint32_t)p[off] << 24 | (uint32_t)p[off + 1] << 16 |
                       (uint32_t)p[off + 2] << 8 | (uint32_t)p[off + 3];
    off += 4;
    if (off + nal_len > total) break;
    appendStartCode(out);
    out.insert(out.end(), p + off, p + off + nal_len);
    off += nal_len;
  }
}

}  // namespace

// hpp 里的 friend —— 干净访问私有回调,不玩布局花活。
struct VTEncoderTrampoline {
  static VTEncoder::FrameCallback& cb(VTEncoder* e) { return e->on_frame_; }
};

namespace {

void compressionCallback(void* user, void* /*src*/, OSStatus status,
                         VTEncodeInfoFlags /*flags*/, CMSampleBufferRef sample) {
  auto* self = static_cast<VTEncoder*>(user);
  if (status != noErr || !sample || !CMSampleBufferDataIsReady(sample)) return;

  const bool key = isKeyframe(sample);
  static thread_local std::vector<uint8_t> annexb;
  sampleToAnnexB(sample, key, annexb);
  if (annexb.empty()) return;

  CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
  uint64_t pts90 =
      (pts.timescale > 0)
          ? static_cast<uint64_t>(pts.value) * 90000ull / pts.timescale
          : 0;

  auto& cb = VTEncoderTrampoline::cb(self);
  if (cb) cb(annexb, key, pts90);
}

}  // namespace

VTEncoder::~VTEncoder() { shutdown(); }

bool VTEncoder::initialize(const Config& cfg, FrameCallback on_frame) {
  shutdown();
  cfg_      = cfg;
  on_frame_ = std::move(on_frame);
  impl_     = new Impl();

  CFMutableDictionaryRef spec = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  // 请求硬件编码;Apple Silicon 上 H.264 硬编必有。
  CFDictionarySetValue(
      spec, kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder,
      kCFBooleanTrue);

  OSStatus st = VTCompressionSessionCreate(
      kCFAllocatorDefault, static_cast<int32_t>(cfg.width),
      static_cast<int32_t>(cfg.height), kCMVideoCodecType_H264, spec, nullptr,
      nullptr, compressionCallback, this, &impl_->session);
  CFRelease(spec);
  if (st != noErr || !impl_->session) {
    spdlog::error("VTEncoder: VTCompressionSessionCreate failed ({})", st);
    return false;
  }

  auto setI32 = [&](CFStringRef key, int32_t v) {
    CFNumberRef n = CFNumberCreate(nullptr, kCFNumberSInt32Type, &v);
    VTSessionSetProperty(impl_->session, key, n);
    CFRelease(n);
  };
  // 实时低延迟:Baseline 去 B 帧,浏览器路径最稳。
  VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_RealTime,
                       kCFBooleanTrue);
  VTSessionSetProperty(impl_->session, kVTCompressionPropertyKey_ProfileLevel,
                       kVTProfileLevel_H264_Baseline_AutoLevel);
  VTSessionSetProperty(impl_->session,
                       kVTCompressionPropertyKey_AllowFrameReordering,
                       kCFBooleanFalse);
  setI32(kVTCompressionPropertyKey_AverageBitRate,
         static_cast<int32_t>(cfg.bitrate_kbps * 1000));
  setI32(kVTCompressionPropertyKey_MaxKeyFrameInterval,
         static_cast<int32_t>(cfg.keyframe_interval));
  setI32(kVTCompressionPropertyKey_ExpectedFrameRate,
         static_cast<int32_t>(cfg.fps));

  VTCompressionSessionPrepareToEncodeFrames(impl_->session);
  spdlog::info("VTEncoder: H.264 {}x{} @{}fps {}kbps (VideoToolbox)", cfg.width,
               cfg.height, cfg.fps, cfg.bitrate_kbps);
  return true;
}

bool VTEncoder::encodeBGRA(const uint8_t* bgra, uint64_t pts_90khz) {
  if (!impl_ || !impl_->session || !bgra) return false;

  CVPixelBufferRef pixbuf = nullptr;
  const size_t stride = static_cast<size_t>(cfg_.width) * 4;
  // 拷贝进 CVPixelBuffer(VT 管理生命周期;零拷贝优化留作后续)。
  CVReturn cvr = CVPixelBufferCreate(kCFAllocatorDefault, cfg_.width,
                                     cfg_.height, kCVPixelFormatType_32BGRA,
                                     nullptr, &pixbuf);
  if (cvr != kCVReturnSuccess) return false;
  CVPixelBufferLockBaseAddress(pixbuf, 0);
  uint8_t* dst = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixbuf));
  const size_t dst_stride = CVPixelBufferGetBytesPerRow(pixbuf);
  for (u32 row = 0; row < cfg_.height; ++row) {
    std::memcpy(dst + row * dst_stride, bgra + row * stride, stride);
  }
  CVPixelBufferUnlockBaseAddress(pixbuf, 0);

  CMTime pts = CMTimeMake(static_cast<int64_t>(pts_90khz), 90000);
  CFDictionaryRef props = nullptr;
  if (force_keyframe_) {
    const void* keys[]   = {kVTEncodeFrameOptionKey_ForceKeyFrame};
    const void* values[] = {kCFBooleanTrue};
    props = CFDictionaryCreate(nullptr, keys, values, 1,
                               &kCFTypeDictionaryKeyCallBacks,
                               &kCFTypeDictionaryValueCallBacks);
    force_keyframe_ = false;
  }
  OSStatus st = VTCompressionSessionEncodeFrame(
      impl_->session, pixbuf, pts, kCMTimeInvalid, props, nullptr, nullptr);
  if (props) CFRelease(props);
  CVPixelBufferRelease(pixbuf);
  return st == noErr;
}

void VTEncoder::shutdown() {
  if (impl_) {
    if (impl_->session) {
      VTCompressionSessionCompleteFrames(impl_->session, kCMTimeInvalid);
      VTCompressionSessionInvalidate(impl_->session);
      CFRelease(impl_->session);
    }
    delete impl_;
    impl_ = nullptr;
  }
}

}  // namespace hongtu::cloud

#endif  // __APPLE__
