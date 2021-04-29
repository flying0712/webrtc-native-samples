#include <iostream>
#include <fstream>
#include <memory>

#include "c_encoder_interface.h"
#include "c_encoder_nv_impl.h"

// 全流程：从本地存储了yuv像素的文件逐帧读取并编码，再将编码后h264文件保存到本地
// 全流程全部调用接口CEncoder中接口
// struct CEonder nv_c_encoder = c_encoder;

static int test_nvenc(void) {

  if (!nv_c_encoder.is_supported()) {
    return -1;
  }

  try {
    void *nv_enc = nv_c_encoder.create();
    struct CEncoderConfig encoder_config = {
        .width = 544,
        .height = 960,
        .framerate = 25,
        .bitrate = 100000,
        .gop = 30,
        .codec = "h264",
        .format = NV_ENC_BUFFER_FORMAT_NV12,

    };
    nv_c_encoder.init(nv_enc, &encoder_config);

    // NvThread thEncode(std::thread(EncodeProc, cuDevice, nWidth, nHeight,
    // eInputFormat, &encodeCLIOptions, bBgra64, szInFilePath, szMediaUri,
    // std::ref(encExceptionPtr)));
    const char *szInFilePath = "/home/lei/myvideos/test_h264_5s_nv12.yuv";
    std::ifstream fpIn(szInFilePath, std::ifstream::in | std::ifstream::binary);
    if (!fpIn) {
      std::cout << "Unable to open input file: " << szInFilePath << std::endl;
      return -1;
    }

    const char *szOutFilePath = "/home/lei/myvideos/enc_test_0203.h264";
    std::ofstream fpOut(szOutFilePath, std::ios::out | std::ios::binary);

    int nHostFrameSize = nv_c_encoder.get_framesize(nv_enc);
    std::unique_ptr<uint8_t[]> pHostFrame(new uint8_t[nHostFrameSize]);

    int nFrame = 0;
    std::streamsize nRead = 0;
    int i = 0;
    // FFmpegStreamer streamer(AV_CODEC_ID_H264, nWidth, nHeight, 25,
    // szMediaPath);
    do {
      std::vector<std::vector<uint8_t>> vPacket;
      // int val = std::rand() % 20;
      // usleep(val * 1000);
      nRead =
          fpIn.read(reinterpret_cast<char *>(pHostFrame.get()), nHostFrameSize)
              .gcount();
      if (nRead == nHostFrameSize) {
        ++i;
        if (i && i % 20 == 1) {
          nv_c_encoder.set_bitrate(nv_enc, 200000);
        }

        int max_buffer_size = nv_c_encoder.get_framesize(nv_enc) * 4;
        std::shared_ptr<uint8_t> out_buffer(new uint8_t[max_buffer_size]);

        int frame_size = nv_c_encoder.encode(nv_enc, pHostFrame.get(),
                                             out_buffer.get(), max_buffer_size);
        if (frame_size > 0) {
          nFrame++;
          fpOut.write(reinterpret_cast<char *>(out_buffer.get()), frame_size);
        }
      } else {
        // nv_enc->nvenc->EndEncode(vPacket);
        std::cout << "nRead = " << nRead << std::endl;
      }

    } while (nRead == nHostFrameSize);

    nv_c_encoder.destroy(&nv_enc);
    fpIn.close();
    fpOut.close();

    std::cout << std::flush << "Total frames encoded: " << nFrame << std::endl
              << std::flush;
  } catch (const std::exception &e) {
    std::cout << e.what();
  }

  return 0;
}

int main() {
  // test1: CEncoder内部test()测试
  nv_c_encoder.test();

  // test2: 外部调用测试
  test_nvenc();
}