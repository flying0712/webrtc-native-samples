#include "api/create_peerconnection_factory.h"
#include "api/video/i420_buffer.h"
#include <chrono>
#include <iostream>

#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"

#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_factory.h"

#include "media/base/video_adapter.h"
#include "media/base/video_broadcaster.h"

#include "i420_creator.h"

#include "pc/video_track_source.h"
#include "stdio.h"

// static rtc::LoggingSeverity sev = !writable() ? rtc::LS_INFO :
// rtc::LS_VERBOSE;

static auto g_signal_thread = rtc::Thread::CreateWithSocketServer();

static rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
    g_peer_connection_factory = nullptr;

rtc::scoped_refptr<webrtc::PeerConnectionInterface> get_default_peer_connection(
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory,
    rtc::Thread *signaling_thread, webrtc::PeerConnectionObserver *observer) {
  webrtc::PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  config.enable_dtls_srtp = false;
  // webrtc::PeerConnectionInterface::IceServer server;
  // server.uri = GetPeerConnectionString();
  // config.servers.push_back(server);

  auto peer_connection =
      factory->CreatePeerConnection(config, nullptr, nullptr, observer);
  assert(peer_connection);
  return peer_connection;
}

static int64_t cur_time() {
  int64_t time_cur = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  return time_cur;
}

class VideoSourceMock : public rtc::VideoSourceInterface<webrtc::VideoFrame> {
public:
  VideoSourceMock()
      : i420_creator_(std::bind(&VideoSourceMock::on_frame, this,
                                std::placeholders::_1)) {
    i420_creator_.set_resolution(input_w_, input_h_);
    // i420_creator_.set_resolution(640,360);
    i420_creator_.run(30);
  }

  void on_frame(I420Creator::I420Frame frame) {
    static int i = 0;
    RTC_LOG(LS_INFO) << "[info] sending frame, no:" << i++;

    rtc::scoped_refptr<webrtc::I420Buffer> buffer = webrtc::I420Buffer::Copy(
        input_w_, input_h_, frame->data(), input_w_,
        frame->data() + input_w_ * input_h_, (input_h_ + 1) / 2,
        frame->data() + input_w_ * input_h_ + input_w_ * input_h_ / 4,
        (input_h_ + 1) / 2);
    webrtc::VideoFrame captureFrame =
        webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            // .set_timestamp_ms(int64_t timestamp_ms)
            // .set_timestamp_rtp(0)
            .set_timestamp_ms(rtc::TimeMillis())
            .set_rotation(webrtc::kVideoRotation_0)
            .set_id(i - 1)
            .build();
    // captureFrame.set_ntp_time_ms(cur_time());
    // TODO:convert i420 to 'videoframe'
    broadcaster_.OnFrame(captureFrame);
    printf("==============>capture: timestamp_cap=%lu\n",
           captureFrame.render_time_ms());
  }

private:
  void AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame> *sink,
                       const rtc::VideoSinkWants &wants) override {
    broadcaster_.AddOrUpdateSink(sink, wants);
    (void)video_adapter_; // we willn't use adapter at this demo
  }
  void RemoveSink(rtc::VideoSinkInterface<webrtc::VideoFrame> *sink) override {
    broadcaster_.RemoveSink(sink);
    (void)video_adapter_; // we willn't use adapter at this demo
  }

private:
  rtc::VideoBroadcaster broadcaster_;
  cricket::VideoAdapter video_adapter_;
  I420Creator i420_creator_;
  int input_w_ = 640;
  int input_h_ = 360;
};

class VideoTrack : public webrtc::VideoTrackSource {
public:
  VideoTrack() : webrtc::VideoTrackSource(false) {
    my_source_ = std::make_unique<VideoSourceMock>();
  }

protected:
  rtc::VideoSourceInterface<webrtc::VideoFrame> *source() override {
    return my_source_.get();
  }

private:
  std::unique_ptr<VideoSourceMock> my_source_;
};

class DummySetSessionDescriptionObserver
    : public webrtc::SetSessionDescriptionObserver {
public:
  static DummySetSessionDescriptionObserver *Create() {
    return new rtc::RefCountedObject<DummySetSessionDescriptionObserver>();
  }
  virtual void OnSuccess() {}
  virtual void OnFailure(webrtc::RTCError error) { assert(false); }
};

class VideoStreamReceiver : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
  int rev_frame_index = 0;

  void SaveVideoFrameToFile(const webrtc::VideoFrame &frame, std::string file) {
    rtc::scoped_refptr<webrtc::VideoFrameBuffer> vfb =
        frame.video_frame_buffer();
    static FILE *fp = fopen(file.c_str(), "wb+");
    if (fp != NULL) {
      fwrite(vfb.get()->GetI420()->DataY(), 1, frame.height() * frame.width(),
             fp);
      fwrite(vfb.get()->GetI420()->DataU(), 1,
             frame.height() * frame.width() / 4, fp);
      fwrite(vfb.get()->GetI420()->DataV(), 1,
             frame.height() * frame.width() / 4, fp);
      fflush(fp);

      // RTC_LOG_F(LS_VERBOSE)  << cur_time() << ": write to file success\n";
      RTC_LOG_F(LS_INFO) << cur_time() << ": write to file success\n";
    }
  }
  void OnFrame(const webrtc::VideoFrame &frame) override {

    printf("==============>rev frame.timestamp_rtp=%u, index=%d\n",
           frame.timestamp(), ++rev_frame_index);
    // printf("==============>rev frame.ntp_time_ms=%lu\n",
    // frame.ntp_time_ms()); printf("==============>rev
    // frame.timestamp_us=%lu\n", frame.timestamp_us());
    RTC_LOG(LS_INFO) << this << "[info] received a frame, id:" << frame.id();
    std::string outfile = "/home/lei/tmp/rev_frame.dump";
    SaveVideoFrameToFile(frame, outfile);
  }
};

class SimpleClient : public webrtc::PeerConnectionObserver,
                     public webrtc::CreateSessionDescriptionObserver {
public:
  SimpleClient(bool sending) {
    if (!sending) {
      RTC_LOG(LS_INFO) << "receiver = " << this;
      video_receiver_ = std::make_shared<VideoStreamReceiver>();
    } else {
      RTC_LOG(LS_INFO) << "sender = " << this;
      ;
    }
  }
  ~SimpleClient() {
    if (peer_connection_) {
      peer_connection_->Close();
      peer_connection_ = nullptr;
    }
  }
  void bind_peerconnection(
      rtc::scoped_refptr<webrtc::PeerConnectionInterface> conn) {
    peer_connection_ = conn;
  }
  void set_other(SimpleClient *receiver) { other_ = receiver; }
  void start() {
    g_signal_thread->Invoke<void>(RTC_FROM_HERE, [this]() {
      peer_connection_->CreateOffer(
          this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    });
  }
  void on_ice_candidate(const webrtc::IceCandidateInterface *candidate) {
    peer_connection_->AddIceCandidate(candidate);
  }
  void on_sdp(webrtc::SessionDescriptionInterface *desc) {
    peer_connection_->SetRemoteDescription(
        DummySetSessionDescriptionObserver::Create(), desc);
    if (desc->GetType() == webrtc::SdpType::kOffer) {
      peer_connection_->CreateAnswer(
          this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    }
  }

protected:
  void OnAddStream(
      rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {
    RTC_LOG(LS_INFO) << this << "[info] on add stream, id:" << stream->id();
  }
  void
  OnAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
             const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>
                 &streams) override {
    auto track = receiver->track().get();

    RTC_LOG(LS_INFO) << this << "[info] on add track,kind:" << track->kind();

    if (track->kind() == "video" && video_receiver_) {
      auto cast_track = static_cast<webrtc::VideoTrackInterface *>(track);
      cast_track->AddOrUpdateSink(video_receiver_.get(), rtc::VideoSinkWants());
    }
  }
  void OnIceCandidate(const webrtc::IceCandidateInterface *candidate) override {
    peer_connection_->AddIceCandidate(candidate);
    std::string candidate_str;
    candidate->ToString(&candidate_str);
    RTC_LOG(LS_INFO) << this
                     << ": OnIceCandidate: candidate_str=" << candidate_str;
    /* sending ice to remote */
    other_->on_ice_candidate(candidate);
  }
  // unused
  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override {}
  void OnRenegotiationNeeded() override {}
  void OnDataChannel(
      rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override {}
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}

protected:
  virtual void OnSuccess(webrtc::SessionDescriptionInterface *desc) override {
    peer_connection_->SetLocalDescription(
        DummySetSessionDescriptionObserver::Create(), desc);

    std::string sdp_str;
    desc->ToString(&sdp_str);
    RTC_LOG(LS_INFO) << this << ": OnSuccess: sdp_str=" << sdp_str;
    RTC_LOG_F(LS_INFO) << cur_time() << ": " << this
                       << ": OnSuccess: sdp_str=" << sdp_str;
    /* sending sdp to remote */
    auto sdp_cp =
        webrtc::CreateSessionDescription(desc->GetType(), sdp_str, nullptr);
    other_->on_sdp(sdp_cp.release());
  }
  virtual void OnFailure(webrtc::RTCError error) {
    RTC_LOG(LS_INFO) << this << "[error] err:" << error.message();
    assert(false);
  }

private:
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_ =
      nullptr;
  std::shared_ptr<VideoStreamReceiver> video_receiver_;
  SimpleClient *other_ = nullptr;
};

int main() {
  g_signal_thread->Start();
  g_peer_connection_factory = webrtc::CreatePeerConnectionFactory(
      nullptr /* network_thread */, nullptr /* worker_thread */,
      g_signal_thread.get() /* signaling_thread */, nullptr /* default_adm */,
      webrtc::CreateBuiltinAudioEncoderFactory(),
      webrtc::CreateBuiltinAudioDecoderFactory(),
      webrtc::CreateBuiltinVideoEncoderFactory(),
      webrtc::CreateBuiltinVideoDecoderFactory(), nullptr /* audio_mixer */,
      nullptr /* audio_processing */);

  // rtc::LogMessage::ConfigureLogging("verbose tstamp thread");
  // rtc::LogMessage::ConfigureLogging("none debug tstamp thread");
  rtc::LogMessage::ConfigureLogging("warning tstamp thread");
  rtc::scoped_refptr<SimpleClient> sender =
      new rtc::RefCountedObject<SimpleClient>(true);
  rtc::scoped_refptr<SimpleClient> receiver =
      new rtc::RefCountedObject<SimpleClient>(false);

  auto video_souce = new rtc::RefCountedObject<VideoTrack>();
  auto video_track =
      g_peer_connection_factory->CreateVideoTrack("video", video_souce);
  auto peer_connection1 = get_default_peer_connection(
      g_peer_connection_factory, g_signal_thread.get(), sender.get());
  peer_connection1->AddTrack(video_track, {"stream1"});

  auto peer_connection2 = get_default_peer_connection(
      g_peer_connection_factory, g_signal_thread.get(), receiver.get());

  sender->bind_peerconnection(peer_connection1);
  receiver->bind_peerconnection(peer_connection2);

  sender->set_other(receiver.get());
  receiver->set_other(sender.get());

  peer_connection1 = nullptr; // decrease ref
  peer_connection2 = nullptr; // decrease ref

  sender->start();

  std::string console;
  while (console != "exit") {
    std::cin >> console;
  }

  g_signal_thread->Invoke<void>(RTC_FROM_HERE, [&sender, &receiver]() {
    sender = nullptr;
    receiver = nullptr;
  });
  g_signal_thread->Stop();

  return 0;
}
