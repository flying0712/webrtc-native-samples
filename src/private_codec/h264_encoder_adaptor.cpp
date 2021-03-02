
// #ifdef WEBRTC_USE_H264

#include "modules/video_coding/codecs/h264/h264_encoder_impl.h"

#include "c_encoder_interface.h"
#include "c_encoder_nv_impl.h"

#include <limits>
#include <string>

// #include "common_video/h264/h264_common.h"

#include "absl/strings/match.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "modules/video_coding/utility/simulcast_rate_allocator.h"
#include "modules/video_coding/utility/simulcast_utility.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/metrics.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/scale.h"

#include "third_party/openh264/src/codec/api/svc/codec_api.h"
#include "third_party/openh264/src/codec/api/svc/codec_app_def.h"
#include "third_party/openh264/src/codec/api/svc/codec_def.h"
#include "third_party/openh264/src/codec/api/svc/codec_ver.h"



namespace webrtc {

//具体的c_encoder
struct CEncoder* concrete_c_encoder = nv_c_encoder;

namespace {

const bool kOpenH264EncoderDetailedLogging = false;

// QP scaling thresholds.
static const int kLowH264QpThreshold = 24;
static const int kHighH264QpThreshold = 37;

// Used by histograms. Values of entries should not be changed.
enum CEncoderEvent {
  kH264EncoderEventInit = 0,
  kH264EncoderEventError = 1,
  kH264EncoderEventMax = 16,
};

int NumberOfThreads(int width, int height, int number_of_cores) {
  // TODO(hbos): In Chromium, multiple threads do not work with sandbox on Mac,
  // see crbug.com/583348. Until further investigated, only use one thread.
  //  if (width * height >= 1920 * 1080 && number_of_cores > 8) {
  //    return 8;  // 8 threads for 1080p on high perf machines.
  //  } else if (width * height > 1280 * 960 && number_of_cores >= 6) {
  //    return 3;  // 3 threads for 1080p.
  //  } else if (width * height > 640 * 480 && number_of_cores >= 3) {
  //    return 2;  // 2 threads for qHD/HD.
  //  } else {
  //    return 1;  // 1 thread for VGA or less.
  //  }
  // TODO(sprang): Also check sSliceArgument.uiSliceNum om GetEncoderPrams(),
  //               before enabling multithreading here.
  return 1;
}

VideoFrameType ConvertToVideoFrameType(EVideoFrameType type) {
  switch (type) {
    case videoFrameTypeIDR:
      return VideoFrameType::kVideoFrameKey;
    case videoFrameTypeSkip:
    case videoFrameTypeI:
    case videoFrameTypeP:
    case videoFrameTypeIPMixed:
      return VideoFrameType::kVideoFrameDelta;
    case videoFrameTypeInvalid:
      break;
  }
  RTC_NOTREACHED() << "Unexpected/invalid frame type: " << type;
  return VideoFrameType::kEmptyFrame;
}

}  // namespace

// Helper method used by CEncoder::Encode.
// Copies the encoded bytes from |info| to |encoded_image|. The
// |encoded_image->_buffer| may be deleted and reallocated if a bigger buffer is
// required.
//
// After OpenH264 encoding, the encoded bytes are stored in |info| spread out
// over a number of layers and "NAL units". Each NAL unit is a fragment starting
// with the four-byte start code {0,0,0,1}. All of this data (including the
// start codes) is copied to the |encoded_image->_buffer|.
// static void RtpFragmentize(EncodedImage* encoded_image, SFrameBSInfo* info) {
//   // Calculate minimum buffer size required to hold encoded data.
//   size_t required_capacity = 0;
//   size_t fragments_count = 0;
//   for (int layer = 0; layer < info->iLayerNum; ++layer) {
//     const SLayerBSInfo& layerInfo = info->sLayerInfo[layer];
//     for (int nal = 0; nal < layerInfo.iNalCount; ++nal, ++fragments_count) {
//       RTC_CHECK_GE(layerInfo.pNalLengthInByte[nal], 0);
//       // Ensure |required_capacity| will not overflow.
//       RTC_CHECK_LE(layerInfo.pNalLengthInByte[nal],
//                    std::numeric_limits<size_t>::max() - required_capacity);
//       required_capacity += layerInfo.pNalLengthInByte[nal];
//     }
//   }
//   // TODO(nisse): Use a cache or buffer pool to avoid allocation?
//   auto buffer = EncodedImageBuffer::Create(required_capacity);
//   encoded_image->SetEncodedData(buffer);

//   // Iterate layers and NAL units, note each NAL unit as a fragment and copy
//   // the data to |encoded_image->_buffer|.
//   const uint8_t start_code[4] = {0, 0, 0, 1};
//   size_t frag = 0;
//   encoded_image->set_size(0);
//   for (int layer = 0; layer < info->iLayerNum; ++layer) {
//     const SLayerBSInfo& layerInfo = info->sLayerInfo[layer];
//     // Iterate NAL units making up this layer, noting fragments.
//     size_t layer_len = 0;
//     for (int nal = 0; nal < layerInfo.iNalCount; ++nal, ++frag) {
//       // Because the sum of all layer lengths, |required_capacity|, fits in a
//       // |size_t|, we know that any indices in-between will not overflow.
//       RTC_DCHECK_GE(layerInfo.pNalLengthInByte[nal], 4);
//       RTC_DCHECK_EQ(layerInfo.pBsBuf[layer_len + 0], start_code[0]);
//       RTC_DCHECK_EQ(layerInfo.pBsBuf[layer_len + 1], start_code[1]);
//       RTC_DCHECK_EQ(layerInfo.pBsBuf[layer_len + 2], start_code[2]);
//       RTC_DCHECK_EQ(layerInfo.pBsBuf[layer_len + 3], start_code[3]);
//       layer_len += layerInfo.pNalLengthInByte[nal];
//     }
//     // Copy the entire layer's data (including start codes).
//     memcpy(buffer->data() + encoded_image->size(), layerInfo.pBsBuf, layer_len);
//     encoded_image->set_size(encoded_image->size() + layer_len);
//   }
// }

static void RtpFragmentize(EncodedImage *encoded_image,
                            const VideoFrameBuffer &frame_buffer,
                            std::vector<uint8_t> &frame_packet,
                            RTPFragmentationHeader *frag_header)
{
    size_t required_capacity = 0;
    encoded_image->set_size(0);

    required_capacity = frame_packet.size();
    encoded_image->SetEncodedData(EncodedImageBuffer::Create(required_capacity));

    // TODO(nisse): Use a cache or buffer pool to avoid allocation?
    encoded_image->SetEncodedData(EncodedImageBuffer::Create(required_capacity));

    memcpy(encoded_image->data(), &frame_packet[0], frame_packet.size());

    std::vector<webrtc::H264::NaluIndex> nalus = webrtc::H264::FindNaluIndices(
        encoded_image->data(), encoded_image->size());
    size_t fragments_count = nalus.size();
    // const uint8_t start_code[4] = {0, 0, 0, 1};
    frag_header->VerifyAndAllocateFragmentationHeader(fragments_count);
    for (size_t i = 0; i < nalus.size(); i++)
    {
        frag_header->fragmentationOffset[i] = nalus[i].payload_start_offset;
        frag_header->fragmentationLength[i] = nalus[i].payload_size;
    }
}


CEncoder::CEncoder(const cricket::VideoCodec& codec)
    : packetization_mode_(H264PacketizationMode::SingleNalUnit),
      max_payload_size_(0),
      number_of_cores_(0),
      encoded_image_callback_(nullptr),
      has_reported_init_(false),
      has_reported_error_(false) {
  RTC_CHECK(absl::EqualsIgnoreCase(codec.name, cricket::kH264CodecName));
  std::string packetization_mode_string;
  if (codec.GetParam(cricket::kH264FmtpPacketizationMode,
                     &packetization_mode_string) &&
      packetization_mode_string == "1") {
    packetization_mode_ = H264PacketizationMode::NonInterleaved;
  }
  downscaled_buffers_.reserve(kMaxSimulcastStreams - 1);
  encoded_images_.reserve(kMaxSimulcastStreams);
  c_encoders_data_.reserve(kMaxSimulcastStreams);
  configurations_.reserve(kMaxSimulcastStreams);
  tl0sync_limit_.reserve(kMaxSimulcastStreams);
}

CEncoder::~CEncoder() {
  Release();
}

int32_t CEncoder::InitEncode(const VideoCodec* inst,
                                    const VideoEncoder::Settings& settings) {
  ReportInit();
  if (!inst || inst->codecType != kVideoCodecH264) {
    ReportError();
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  if (inst->maxFramerate == 0) {
    ReportError();
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  if (inst->width < 1 || inst->height < 1) {
    ReportError();
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }

  int32_t release_ret = Release();
  if (release_ret != WEBRTC_VIDEO_CODEC_OK) {
    ReportError();
    return release_ret;
  }

  int number_of_streams = SimulcastUtility::NumberOfSimulcastStreams(*inst);
  bool doing_simulcast = (number_of_streams > 1);

  if (doing_simulcast &&
      !SimulcastUtility::ValidSimulcastParameters(*inst, number_of_streams)) {
    return WEBRTC_VIDEO_CODEC_ERR_SIMULCAST_PARAMETERS_NOT_SUPPORTED;
  }
  downscaled_buffers_.resize(number_of_streams - 1);
  encoded_images_.resize(number_of_streams);
  c_encoders_data_.resize(number_of_streams);
  pictures_.resize(number_of_streams);
  configurations_.resize(number_of_streams);
  tl0sync_limit_.resize(number_of_streams);

  number_of_cores_ = settings.number_of_cores;
  max_payload_size_ = settings.max_payload_size;
  codec_ = *inst;

  // Code expects simulcastStream resolutions to be correct, make sure they are
  // filled even when there are no simulcast layers.
  if (codec_.numberOfSimulcastStreams == 0) {
    codec_.simulcastStream[0].width = codec_.width;
    codec_.simulcastStream[0].height = codec_.height;
  }

  for (int i = 0, idx = number_of_streams - 1; i < number_of_streams;
       ++i, --idx) {
 
    // Create concrete concrete_c_encoder_data
    void *tmp_c_encoders_data = concrete_c_encoder->create();
    

    RTC_DCHECK(c_enc);
    // if (kOpenH264EncoderDetailedLogging) {
    //   int trace_level = WELS_LOG_DETAIL;
    //   openh264_encoder->SetOption(ENCODER_OPTION_TRACE_LEVEL, &trace_level);
    // }
    // else WELS_LOG_DEFAULT is used by default.

    // Store h264 encoder.
    c_encoders_data_[i] = tmp_c_encoders_data;

    // Set internal settings from codec_settings
    configurations_[i].simulcast_idx = idx;
    configurations_[i].sending = false;
    configurations_[i].width = codec_.simulcastStream[idx].width;
    configurations_[i].height = codec_.simulcastStream[idx].height;
    configurations_[i].max_frame_rate = static_cast<float>(codec_.maxFramerate);
    configurations_[i].frame_dropping_on = codec_.H264()->frameDroppingOn;
    configurations_[i].key_frame_interval = codec_.H264()->keyFrameInterval;
    configurations_[i].num_temporal_layers =
        codec_.simulcastStream[idx].numberOfTemporalLayers;

    // Create downscaled image buffers.
    if (i > 0) {
      downscaled_buffers_[i - 1] = I420Buffer::Create(
          configurations_[i].width, configurations_[i].height,
          configurations_[i].width, configurations_[i].width / 2,
          configurations_[i].width / 2);
    }

    // Codec_settings uses kbits/second; encoder uses bits/second.
    configurations_[i].max_bps = codec_.maxBitrate * 1000;
    configurations_[i].target_bps = codec_.startBitrate * 1000;

            CEncoderConfig c_enc_config;
            c_enc_config.codec = "h264";
            // TODO: 改正格式
            c_enc_config.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            c_enc_config.width = configurations_[i].width;
            c_enc_config.height = configurations_[i].height;
            c_enc_config.framerate = (uint32_t)configurations_[i].max_frame_rate;
            c_enc_config.gop = configurations_[i].key_frame_interval * 2;
            c_enc_config.bitrate = configurations_[i].target_bps;
            if (!concrete_c_encoder->init(c_encoders_data_[i], &c_enc_config))
            {
                Release();
                ReportError();

                // TODO: release encoder created

                return WEBRTC_VIDEO_CODEC_ERROR;
            }

    // // Create encoder parameters based on the layer configuration.
    // SEncParamExt encoder_params = CreateEncoderParams(i);

    // // Initialize.
    // if (openh264_encoder->InitializeExt(&encoder_params) != 0) {
    //   RTC_LOG(LS_ERROR) << "Failed to initialize OpenH264 encoder";
    //   Release();
    //   ReportError();
    //   return WEBRTC_VIDEO_CODEC_ERROR;
    // }
    // TODO(pbos): Base init params on these values before submitting.
    int video_format = EVideoFormatType::videoFormatI420;
    // 不在这里配置
    // openh264_encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &video_format);

    // Initialize encoded image. Default buffer size: size of unencoded data.

    const size_t new_capacity =
        CalcBufferSize(VideoType::kI420, codec_.simulcastStream[idx].width,
                       codec_.simulcastStream[idx].height);
    encoded_images_[i].SetEncodedData(EncodedImageBuffer::Create(new_capacity));
    encoded_images_[i]._encodedWidth = codec_.simulcastStream[idx].width;
    encoded_images_[i]._encodedHeight = codec_.simulcastStream[idx].height;
    encoded_images_[i].set_size(0);

    tl0sync_limit_[i] = configurations_[i].num_temporal_layers;
  }

  SimulcastRateAllocator init_allocator(codec_);
  VideoBitrateAllocation allocation =
      init_allocator.Allocate(VideoBitrateAllocationParameters(
          DataRate::KilobitsPerSec(codec_.startBitrate), codec_.maxFramerate));
  SetRates(RateControlParameters(allocation, codec_.maxFramerate));
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t CEncoder::Release() {
  while (!c_encoders_data_.empty()) {
    // ISVCEncoder* openh264_encoder = c_encoders_data_.back();
    // if (openh264_encoder) {
    //   RTC_CHECK_EQ(0, openh264_encoder->Uninitialize());
    //   WelsDestroySVCEncoder(openh264_encoder);

    void* encoder_data = c_encoders_data_.back();
    if(encoder_data){
        // TODO: 可以改为一级指针
      concrete_c_encoder->destroy(&encoder_data);
    }
    }
    c_encoders_data_.pop_back();
  }
  downscaled_buffers_.clear();
  configurations_.clear();
  encoded_images_.clear();
  pictures_.clear();
  tl0sync_limit_.clear();
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t CEncoder::RegisterEncodeCompleteCallback(
    EncodedImageCallback* callback) {
  encoded_image_callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

void CEncoder::SetRates(const RateControlParameters& parameters) {
  if (c_encoders_data_.empty()) {
    RTC_LOG(LS_WARNING) << "SetRates() while uninitialized.";
    return;
  }

  if (parameters.framerate_fps < 1.0) {
    RTC_LOG(LS_WARNING) << "Invalid frame rate: " << parameters.framerate_fps;
    return;
  }

  if (parameters.bitrate.get_sum_bps() == 0) {
    // Encoder paused, turn off all encoding.
    for (size_t i = 0; i < configurations_.size(); ++i) {
      configurations_[i].SetStreamState(false);
    }
    return;
  }

  codec_.maxFramerate = static_cast<uint32_t>(parameters.framerate_fps);

  size_t stream_idx = c_encoders_data_.size() - 1;
  for (size_t i = 0; i < c_encoders_data_.size(); ++i, --stream_idx) {
    // Update layer config.
    configurations_[i].target_bps =
        parameters.bitrate.GetSpatialLayerSum(stream_idx);
    configurations_[i].max_frame_rate = parameters.framerate_fps;

    if (configurations_[i].target_bps) {
      configurations_[i].SetStreamState(true);

      // Update h264 encoder.
      SBitrateInfo target_bitrate;
      memset(&target_bitrate, 0, sizeof(SBitrateInfo));
      target_bitrate.iLayer = SPATIAL_LAYER_ALL,
      target_bitrate.iBitrate = configurations_[i].target_bps;

      concrete_c_encoder->set_bitrate(c_encoders_data_[i], configurations_[i].target_bps);
      concrete_c_encoder->set_framerate(c_encoders_data_[i], (uint32_t)configurations_[i].max_frame_rate);
    } else {
      configurations_[i].SetStreamState(false);
    }
  }
}

int32_t CEncoder::Encode(
    const VideoFrame& input_frame,
    const std::vector<VideoFrameType>* frame_types) {
  if (c_encoders_data_.empty()) {
    ReportError();
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }
  if (!encoded_image_callback_) {
    RTC_LOG(LS_WARNING)
        << "InitEncode() has been called, but a callback function "
           "has not been set with RegisterEncodeCompleteCallback()";
    ReportError();
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  rtc::scoped_refptr<const I420BufferInterface> frame_buffer =
      input_frame.video_frame_buffer()->ToI420();

  bool send_key_frame = false;
  for (size_t i = 0; i < configurations_.size(); ++i) {
    if (configurations_[i].key_frame_request && configurations_[i].sending) {
      send_key_frame = true;
      break;
    }
  }

  if (!send_key_frame && frame_types) {
    for (size_t i = 0; i < configurations_.size(); ++i) {
      const size_t simulcast_idx =
          static_cast<size_t>(configurations_[i].simulcast_idx);
      if (configurations_[i].sending && simulcast_idx < frame_types->size() &&
          (*frame_types)[simulcast_idx] == VideoFrameType::kVideoFrameKey) {
        send_key_frame = true;
        break;
      }
    }
  }

  RTC_DCHECK_EQ(configurations_[0].width, frame_buffer->width());
  RTC_DCHECK_EQ(configurations_[0].height, frame_buffer->height());

  // Encode image for each layer.
  for (size_t i = 0; i < c_encoders_data_.size(); ++i) {

    if (!configurations_[i].sending) {
      continue;
    }
    if (frame_types != nullptr) {
      // Skip frame?
      if ((*frame_types)[i] == VideoFrameType::kEmptyFrame) {
        continue;
      }
    }
    if (send_key_frame) {
      // API doc says ForceIntraFrame(false) does nothing, but calling this
      // function forces a key frame regardless of the |bIDR| argument's value.
      // (If every frame is a key frame we get lag/delays.)
      
      if(!c_encoders_data_.empty() && c_encoders_data_[i]){
          concrete_c_encoder->request_idr(c_encoders_data_[i]);
      }
      
      
      configurations_[i].key_frame_request = false;
    }
    // EncodeFrame output.
    SFrameBSInfo info;
    memset(&info, 0, sizeof(SFrameBSInfo));

    // // Encode!
    // int enc_ret = c_encoders_data_[i]->EncodeFrame(&pictures_[i], &info);
    // if (enc_ret != 0) {
    //   RTC_LOG(LS_ERROR)
    //       << "OpenH264 frame encoding failed, EncodeFrame returned " << enc_ret
    //       << ".";
    //   ReportError();
    //   return WEBRTC_VIDEO_CODEC_ERROR;
        std::vector<uint8_t> frame_packet;

        EncodeFrame((int)i, input_frame, frame_packet);

        if (frame_packet.size() == 0)
        {
            return WEBRTC_VIDEO_CODEC_OK;
        }
        else
        {
            if (frame_packet[4] == 0x67)
            {
                info.eFrameType = videoFrameTypeIDR;
            }
            else if (frame_packet[4] == 0x61)
            {
                info.eFrameType = videoFrameTypeP;
            }
            else
            {
                return WEBRTC_VIDEO_CODEC_OK;
            }
        }
    

    encoded_images_[i]._encodedWidth = configurations_[i].width;
    encoded_images_[i]._encodedHeight = configurations_[i].height;
    encoded_images_[i].SetTimestamp(input_frame.timestamp());

    // 从nvcodec 添加
    encoded_images_[i].ntp_time_ms_ = input_frame.ntp_time_ms();
    encoded_images_[i].capture_time_ms_ = input_frame.render_time_ms();
    encoded_images_[i].rotation_ = input_frame.rotation();
    encoded_images_[i].SetColorSpace(input_frame.color_space());
    encoded_images_[i].content_type_ =
        (codec_.mode == VideoCodecMode::kScreensharing)
            ? VideoContentType::SCREENSHARE
            : VideoContentType::UNSPECIFIED;
    encoded_images_[i].timing_.flags = VideoSendTiming::kInvalid;
 

    encoded_images_[i]._frameType = ConvertToVideoFrameType(info.eFrameType);
    encoded_images_[i].SetSpatialIndex(configurations_[i].simulcast_idx);

    // Split encoded image up into fragments. This also updates
    // |encoded_image_|.
    // RtpFragmentize(&encoded_images_[i], &info);
            
    RTPFragmentationHeader frag_header;
    RtpFragmentize(&encoded_images_[i], *frame_buffer, frame_packet, &frag_header);



    // Encoder can skip frames to save bandwidth in which case
    // |encoded_images_[i]._length| == 0.
    if (encoded_images_[i].size() > 0) {
    //   // Parse QP.
    //   h264_bitstream_parser_.ParseBitstream(encoded_images_[i].data(),
    //                                         encoded_images_[i].size());
    //   h264_bitstream_parser_.GetLastSliceQp(&encoded_images_[i].qp_);
    
    //TODO: 计算c_encorder qp
    encoded_images_[i].qp_ = 10;

      // Deliver encoded image.
      CodecSpecificInfo codec_specific;
      codec_specific.codecType = kVideoCodecH264;
      codec_specific.codecSpecific.H264.packetization_mode =
          packetization_mode_;
      codec_specific.codecSpecific.H264.temporal_idx = kNoTemporalIdx;
      codec_specific.codecSpecific.H264.idr_frame =
          info.eFrameType == videoFrameTypeIDR;
      codec_specific.codecSpecific.H264.base_layer_sync = false;
      if (configurations_[i].num_temporal_layers > 1) {
        const uint8_t tid = info.sLayerInfo[0].uiTemporalId;
        codec_specific.codecSpecific.H264.temporal_idx = tid;
        codec_specific.codecSpecific.H264.base_layer_sync =
            tid > 0 && tid < tl0sync_limit_[i];
        if (codec_specific.codecSpecific.H264.base_layer_sync) {
          tl0sync_limit_[i] = tid;
        }
        if (tid == 0) {
          tl0sync_limit_[i] = configurations_[i].num_temporal_layers;
        }
      }
      encoded_image_callback_->OnEncodedImage(encoded_images_[i],
                                              &codec_specific);
    }
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

void CEncoder::ReportInit() {
  if (has_reported_init_)
    return;
  RTC_HISTOGRAM_ENUMERATION("WebRTC.Video.CEncoder.Event",
                            kH264EncoderEventInit, kH264EncoderEventMax);
  has_reported_init_ = true;
}

void CEncoder::ReportError() {
  if (has_reported_error_)
    return;
  RTC_HISTOGRAM_ENUMERATION("WebRTC.Video.CEncoder.Event",
                            kH264EncoderEventError, kH264EncoderEventMax);
  has_reported_error_ = true;
}

VideoEncoder::EncoderInfo CEncoder::GetEncoderInfo() const {
  EncoderInfo info;
  info.supports_native_handle = false;
  info.implementation_name = "c_encoder";
  info.scaling_settings =
      VideoEncoder::ScalingSettings(kLowH264QpThreshold, kHighH264QpThreshold);
  info.is_hardware_accelerated = true;
  info.has_internal_source = false;
//   info.supports_simulcast = true;
  info.supports_simulcast = false;
  info.preferred_pixel_formats = {VideoFrameBuffer::Type::kI420};
  return info;
}

void CEncoder::LayerConfig::SetStreamState(bool send_stream) {
  if (send_stream && !sending) {
    // Need a key frame if we have not sent this stream before.
    key_frame_request = true;
  }
  sending = send_stream;
}

bool NvEncoder::EncodeFrame(int index, const VideoFrame &input_frame,
                            std::vector<uint8_t> &frame_packet)
{
    frame_packet.clear();

    if (c_encoders_data_.empty() || !c_encoders_data_[index])
    {
        return false;
    }

    if (video_format_ == EVideoFormatType::videoFormatI420)
    {
        if (image_buffer_ != nullptr)
        {
            // 是否需要再转换像素格式？
            // if (webrtc::ConvertFromI420(input_frame, webrtc::VideoType::kARGB, 0,
            //                             image_buffer_.get()) < 0)
            // {
            //     return false;
            // }

            if (webrtc::ConvertFromI420(input_frame, webrtc::VideoType::kNV12, 0,
                                        image_buffer_.get()) < 0)
            {
                return false;
            }


        }
        else
        {
            return false;
        }
    }

    int width = input_frame.width();
    int height = input_frame.height();

            int max_buffer_size = height * width * 4;
            std::shared_ptr<uint8_t> out_buffer(new uint8_t[max_buffer_size]);

            int frame_size = concrete_c_encoder->encode(c_encoders_data_[index], image_buffer_.get(), out_buffer.get(), max_buffer_size);
            if (frame_size > 0)
            {
                frame_packet.resize(frame_size);
                memcpy(&frame_packet[0], out_buffer.get(), frame_size);
            }
        }
    }

    return true;
}

}  // namespace webrtc

// #endif  // WEBRTC_USE_H264
