#include "i420_creator.h"

#include "libyuv.h"
#include "libyuv/scale.h"
#include "rtc_base/logging.h"

#include <cassert>
#include <future>
#include <chrono>

I420Creator::~I420Creator()
{
    running_ = false;
    if(thread_.joinable()){
        thread_.join();
    }
}


void I420Creator::run(int fps) {
  if (running_ || fps == 0) {
    assert(false);
    return;
  }

  running_ = true;
  std::promise<bool> promise;
  auto future = promise.get_future();
  thread_ = std::thread([this, fps, &promise]() {
    promise.set_value(true);
    int cnt = 0;
    while (running_ && cnt++ <= 100) {
      auto duration_ms = 1000 / fps;
      using namespace std::chrono_literals;
      std::this_thread::sleep_for(1ms * duration_ms);
      if (observer_) {
        observer_(my_process());
      }
    }
  });
  pthread_setname_np(thread_.native_handle(), "capture");
  // thread_.detach();
  future.wait();
}

uint8_t limit(int& v, int min, int max)
{
    v = std::min(max, v);
    v = std::max(min, v);
    return static_cast<uint8_t>(v);
}

void rgb_to_i420(const uint8_t* rgb, uint8_t* yuv, size_t size)
{
    assert(size >= 3);
    auto r = rgb[0];
    auto g = rgb[1];
    auto b = rgb[2];

    int y = ((66  * r + 129 * g + 25  * b + 128) >> 8) + 16;
    int u = ((-38 * r - 74  * g + 112 * b + 128) >> 8) + 128;
    int v = ((112 * r - 94  * g - 18  * b + 128) >> 8) + 128;

    yuv[0] = limit(y, 0, 255);
    yuv[1] = limit(u, 0, 255);
    yuv[2] = limit(v, 0, 255);
}


I420Creator::I420Frame I420Creator::process()
{
    const uint8_t colors[6][3] =
    {                   //RGB
        {255, 0, 0},    //red
        {255, 165, 0},  //orange
        {255, 255, 0},  //yellow
        {0, 255, 0},    //Green
        {0, 0, 255},    //Blue
        {160,32,240}    //purple
    };
    static int i = 0;
    i = (i++) % 6;
    auto frame = std::make_shared<std::vector<uint8_t>>();
    frame->resize(static_cast<size_t>(w_ * h_ * 1.5));
    uint8_t* buffer_y = frame->data();
    uint8_t* buffer_u = frame->data() + w_*h_;
    uint8_t* buffer_v = frame->data() + w_*h_ / 4;
    for(size_t i = 0 ;i < w_; i++)
    {
        for(size_t j = 0; j < h_; j++)
        {
            const auto& rgb = colors[i % 6];
            uint8_t yuv[3] = {0};
            rgb_to_i420(rgb, yuv, 3);
            *(buffer_y++) = yuv[0];
            if(j % 2 == 0 && i %2 == 0)
            {
                *(buffer_u++) = yuv[1];
                *(buffer_v++) = yuv[2];
            }
        }
    }
    return frame;
}

I420Creator::I420Frame I420Creator::my_process() {
  // #if 0
  // test for argb to i420, scale
  int width = 1257, height = 688;
  int src_stride_argb = width * 4;

  // read rgb pix from file to src_argb
  auto src_argb = std::make_shared<std::vector<uint8_t>>(width * height * 4);
  assert(src_argb);

  std::string file = "/home/lei/tmp/chunkdata_1257x688.rgba";//ARGB pixel bitmap
  FILE *f = fopen(file.c_str(), "rb");
  if (f) {
    fread(src_argb->data(), 1, width * height * 4, f);
    fclose(f);
    RTC_LOG(LS_INFO) << "read from file success";
  } else {
    RTC_LOG(LS_INFO) << "open error";
  }

  int out_w = width + 1;
  int out_h = height;
  int out_i420_stride_y = out_w;
  int out_i420_stride_u = (out_w) / 2;
  int out_i420_stride_v = (out_w) / 2;

  auto out_i420_frame =
      std::make_shared<std::vector<uint8_t>>(out_w * out_h * 3 / 2);
  uint8_t *out_i420_y = out_i420_frame->data();
  uint8_t *out_i420_u = out_i420_frame->data() + out_w * out_h;
  uint8_t *out_i420_v = out_i420_frame->data() + out_w * out_h * 5 / 4;

  libyuv::ARGBToI420(src_argb->data(), src_stride_argb, out_i420_y,
                     out_i420_stride_y, out_i420_u, out_i420_stride_u,
                     out_i420_v, out_i420_stride_v, width, -height);

  auto scale_i420_frame =
      std::make_shared<std::vector<uint8_t>>(w_ * h_ * 3 / 2);
  auto scale_i420_y = scale_i420_frame->data();
  auto scale_i420_u = scale_i420_frame->data() + w_ * h_;
  auto scale_i420_v = scale_i420_frame->data() + w_ * h_ * 5 / 4;
  libyuv::I420Scale(out_i420_y, out_i420_stride_y, out_i420_u,
                    out_i420_stride_u, out_i420_v, out_i420_stride_v, out_w,
                    out_h, scale_i420_y, w_, scale_i420_u, w_ / 2, scale_i420_v,
                    w_ / 2, w_, h_, libyuv::kFilterLinear);

  //#endif
  return scale_i420_frame;

}
