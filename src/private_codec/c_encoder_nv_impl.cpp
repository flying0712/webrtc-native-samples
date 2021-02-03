#include "c_encoder_interface.h"
#include "c_encoder_nv_impl.h"

#include <cuda.h>
#include <iostream>
#include <iomanip>
#include <exception>
#include <stdexcept>
#include <memory>
#include <functional>
#include <stdint.h>
// #include "NVCodec/NvDecoder/NvDecoder.h"
#include "NvCodec/NvEncoder/NvEncoderCuda.h"
#include "Utils/NvEncoderCLIOptions.h"
#include "Utils/NvCodecUtils.h"
// #include "../Utils/FFmpegStreamer.h"
// #include "../Utils/FFmpegDemuxer.h"
// #include "Utils/ColorSpace.h"

#include <cstdint>
#include <string>

simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();

struct nvenc_data
{
    CUdevice cuDevice = 0;
    CUdeviceptr dpBgraFrame = 0;
    CUcontext *cuContextPtr = nullptr;

    std::mutex mutex;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t framerate = 0;
    uint32_t bitrate = 0;
    uint32_t gop = 0;
    std::string codec;
    NV_ENC_BUFFER_FORMAT format = NV_ENC_BUFFER_FORMAT_NV12;

    NvEncoderCuda *nvenc = nullptr;
};

static bool is_supported(void)
{
    return true;
}

static void *nvenc_create()
{
    if (!is_supported())
    {
        return nullptr;
    }

    struct nvenc_data *nv_enc = new nvenc_data;

    ck(cuInit(0));

    nv_enc->cuContextPtr = new CUcontext();
    ck(cuCtxCreate(nv_enc->cuContextPtr, 0, nv_enc->cuDevice));

    return nv_enc;
}

static void nvenc_destroy(void **nvenc_data)
{
    struct nvenc_data *enc = (struct nvenc_data *)(*nvenc_data);

    enc->mutex.lock();

    if (enc->nvenc != nullptr)
    {
        enc->nvenc->DestroyEncoder();
        delete (enc->nvenc);
        enc->nvenc = nullptr;
    }

    ck(cuMemFree(enc->dpBgraFrame));
    enc->dpBgraFrame = 0;

    enc->mutex.unlock();

    delete ((struct nvenc_data *)(*nvenc_data));
    *nvenc_data = nullptr;
}

static bool nvenc_init(void *nvenc_data, void *encoder_config)
{
    if (nvenc_data == nullptr)
    {
        return false;
    }

    struct nvenc_data *nv_enc = (struct nvenc_data *)nvenc_data;
    struct CEncoderConfig *config = (struct CEncoderConfig *)encoder_config;

    std::lock_guard<std::mutex> locker(nv_enc->mutex);
    if (nv_enc->nvenc != nullptr)
    {
        return false;
    }
    if (nv_enc->nvenc != nullptr)
    {
        return false;
    }

    nv_enc->width = config->width;
    nv_enc->height = config->height;
    nv_enc->framerate = config->framerate;
    nv_enc->format = config->format;
    nv_enc->codec = config->codec;
    // nv_enc->gop = config->gop;
    nv_enc->bitrate = config->bitrate;

    // NV_ENC_BUFFER_FORMAT eInputFormat = NV_ENC_BUFFER_FORMAT_NV12;
    // int nWidth = 544, nHeight = 960;
    nv_enc->nvenc = new NvEncoderCuda(*(nv_enc->cuContextPtr), nv_enc->width, nv_enc->height, nv_enc->format);

    NV_ENC_INITIALIZE_PARAMS initializeParams = {NV_ENC_INITIALIZE_PARAMS_VER};
    NV_ENC_CONFIG encodeConfig = {NV_ENC_CONFIG_VER};
    initializeParams.encodeConfig = &encodeConfig;

    GUID codecId = NV_ENC_CODEC_H264_GUID;
    if (nv_enc->codec == "h264")
    {
        codecId = NV_ENC_CODEC_H264_GUID;
    }
    else if (nv_enc->codec == "hevc")
    {
        codecId = NV_ENC_CODEC_HEVC_GUID;
    }
    else
    {
        printf("[nvenc] Error: Unsupported codec. \n");
        return false;
    }

    // 默认h264及配置文件
    //  NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID
    nv_enc->nvenc->CreateDefaultEncoderParams(&initializeParams, codecId, NV_ENC_PRESET_DEFAULT_GUID);

    encodeConfig.gopLength = NVENC_INFINITE_GOPLENGTH;
    encodeConfig.frameIntervalP = 1;
    encodeConfig.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;

    encodeConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ;
    encodeConfig.rcParams.averageBitRate = nv_enc->bitrate;

    // encodeConfig.rcParams.averageBitRate = (static_cast<unsigned int>(5.0f * initializeParams.encodeWidth * initializeParams.encodeHeight) / (1280 * 720)) * 100000;
    encodeConfig.rcParams.vbvBufferSize = (encodeConfig.rcParams.averageBitRate * initializeParams.frameRateDen / initializeParams.frameRateNum) * 5;
    encodeConfig.rcParams.maxBitRate = encodeConfig.rcParams.averageBitRate;
    encodeConfig.rcParams.vbvInitialDelay = encodeConfig.rcParams.vbvBufferSize;

    NvEncoderInitParam pEncodeCLIOptions;
    pEncodeCLIOptions.SetInitParams(&initializeParams, nv_enc->format);

    nv_enc->nvenc->CreateEncoder(&initializeParams);

    ck(cuMemAlloc(&(nv_enc->dpBgraFrame), nv_enc->width * nv_enc->height * 8));

    return true;
}

int nvenc_set_bitrate(void *nvenc_data, uint32_t bitrate_bps)
{
    if (nvenc_data == nullptr)
    {
        return 0;
    }

    struct nvenc_data *nv_enc = (struct nvenc_data *)nvenc_data;

    std::lock_guard<std::mutex> locker(nv_enc->mutex);

    if (nv_enc->nvenc != nullptr)
    {
        NV_ENC_RECONFIGURE_PARAMS reconfigureParams = {NV_ENC_RECONFIGURE_PARAMS_VER};
        NV_ENC_CONFIG encodeConfig = {NV_ENC_CONFIG_VER};
        reconfigureParams.forceIDR = true;
        reconfigureParams.reInitEncodeParams = {NV_ENC_INITIALIZE_PARAMS_VER};
        reconfigureParams.reInitEncodeParams.encodeConfig = &encodeConfig;
        nv_enc->nvenc->GetInitializeParams(&reconfigureParams.reInitEncodeParams);

        reconfigureParams.reInitEncodeParams.encodeConfig->rcParams.averageBitRate = bitrate_bps;
        reconfigureParams.reInitEncodeParams.encodeConfig->rcParams.vbvBufferSize = reconfigureParams.reInitEncodeParams.encodeConfig->rcParams.averageBitRate *
                                                                                    reconfigureParams.reInitEncodeParams.frameRateDen / reconfigureParams.reInitEncodeParams.frameRateNum;
        reconfigureParams.reInitEncodeParams.encodeConfig->rcParams.vbvInitialDelay = reconfigureParams.reInitEncodeParams.encodeConfig->rcParams.vbvBufferSize;

        nv_enc->nvenc->Reconfigure(&reconfigureParams);
    }

    return 0;
}

int nvenc_set_framerate(void *nvenc_data, uint32_t framerate)
{
    if (nvenc_data == nullptr)
    {
        return 0;
    }

    struct nvenc_data *enc = (struct nvenc_data *)nvenc_data;

    std::lock_guard<std::mutex> locker(enc->mutex);

    if (enc->nvenc != nullptr)
    {
        NV_ENC_RECONFIGURE_PARAMS reconfigureParams = {NV_ENC_RECONFIGURE_PARAMS_VER};
        NV_ENC_CONFIG encodeConfig = {NV_ENC_CONFIG_VER};
        reconfigureParams.forceIDR = true;
        reconfigureParams.reInitEncodeParams = {NV_ENC_INITIALIZE_PARAMS_VER};
        reconfigureParams.reInitEncodeParams.encodeConfig = &encodeConfig;
        enc->nvenc->GetInitializeParams(&reconfigureParams.reInitEncodeParams);

        //不改变frameRateDen（默认为1），仅改变framerate； framerate = frameRateNum / frameRateDen
        reconfigureParams.reInitEncodeParams.frameRateNum = framerate;

        enc->nvenc->Reconfigure(&reconfigureParams);
    }

    return 0;
}

int nvenc_request_idr(void *nvenc_data)
{
    if (nvenc_data == nullptr)
    {
        return 0;
    }

    struct nvenc_data *enc = (struct nvenc_data *)nvenc_data;

    std::lock_guard<std::mutex> locker(enc->mutex);

    if (enc->nvenc != nullptr)
    {
        // enc->nvenc->ForceIDR();

        NV_ENC_RECONFIGURE_PARAMS reconfigureParams = {NV_ENC_RECONFIGURE_PARAMS_VER};
        NV_ENC_CONFIG encodeConfig = {NV_ENC_CONFIG_VER};
        reconfigureParams.version = NV_ENC_RECONFIGURE_PARAMS_VER;
        reconfigureParams.forceIDR = true;
        reconfigureParams.reInitEncodeParams = {NV_ENC_INITIALIZE_PARAMS_VER};
        reconfigureParams.reInitEncodeParams.encodeConfig = &encodeConfig;
        enc->nvenc->GetInitializeParams(&reconfigureParams.reInitEncodeParams);

        enc->nvenc->Reconfigure(&reconfigureParams);
    }

    return 0;
}

int nvenc_get_framesize(void *nvenc_data)
{
    struct nvenc_data *enc = (struct nvenc_data *)nvenc_data;
    return enc->nvenc->GetFrameSize();
}

int nvenc_encode(void *nvenc_data, uint8_t *in_buf, uint8_t *out_buf, uint32_t out_buf_size)
{
    if (nvenc_data == nullptr)
    {
        return -1;
    }

    struct nvenc_data *enc = (struct nvenc_data *)nvenc_data;

    std::lock_guard<std::mutex> locker(enc->mutex);

    if (enc->nvenc == nullptr)
    {
        return -1;
    }

    std::vector<std::vector<uint8_t>> vPacket;
    const NvEncInputFrame *encoderInputFrame = enc->nvenc->GetNextInputFrame();

    NvEncoderCuda::CopyToDeviceFrame(*(enc->cuContextPtr), in_buf, 0,
                                     (CUdeviceptr)encoderInputFrame->inputPtr,
                                     (int)encoderInputFrame->pitch,
                                     enc->nvenc->GetEncodeWidth(),
                                     enc->nvenc->GetEncodeHeight(),
                                     CU_MEMORYTYPE_HOST,
                                     encoderInputFrame->bufferFormat,
                                     encoderInputFrame->chromaOffsets,
                                     encoderInputFrame->numChromaPlanes);
    enc->nvenc->EncodeFrame(vPacket);
    enc->nvenc->EndEncode(vPacket);

    int frame_size = 0;
    for (std::vector<uint8_t> &packet : vPacket)
    {
        if (frame_size + packet.size() < out_buf_size)
        {
            memcpy(out_buf + frame_size, packet.data(), packet.size());
            frame_size += (int)packet.size();
        }
        else
        {
            break;
        }
    }

    return frame_size;
}

int nvenc_get_sequence_params(void *nvenc_data, uint8_t *outBuf, uint32_t max_buf_size)
{
    if (nvenc_data == nullptr)
    {
        return 0;
    }

    struct nvenc_data *enc = (struct nvenc_data *)nvenc_data;

    std::lock_guard<std::mutex> locker(enc->mutex);

    if (enc->nvenc != nullptr)
    {
        std::vector<uint8_t> seq_params;
        enc->nvenc->GetSequenceParams(seq_params);
        if (seq_params.size() > 0 && seq_params.size() < max_buf_size)
        {
            memcpy(outBuf, seq_params.data(), seq_params.size());
            return (int)seq_params.size();
        }
    }

    return 0;
}

// 全流程：从本地存储了yuv像素的文件逐帧读取并编码，再将编码后h264文件保存到本地
// 使用官方示例进行全流程测试，不使用CEncoder中的接口
static bool nvenc_test0(void)
{

    if (!is_supported())
    {
        return false;
    }

    struct nvenc_data *nv_enc = new nvenc_data;

    CUdeviceptr dpFrame = 0, dpBgraFrame = 0;

    try
    {
        ck(cuInit(0));
        // CUdevice cuDevice = 0;
        // nv_enc->cudevicePtr = &cuDevice;
        // // ck(cuDeviceGet(&cuDevice, iGpu));
        // char szDeviceName[80];
        // ck(cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice));
        // std::cout << "GPU in use: " << szDeviceName << std::endl;

        nv_enc->cuContextPtr = new CUcontext();
        ck(cuCtxCreate(nv_enc->cuContextPtr, 0, nv_enc->cuDevice));

        NV_ENC_BUFFER_FORMAT eInputFormat = NV_ENC_BUFFER_FORMAT_NV12;
        int nWidth = 544, nHeight = 960;
        nv_enc->nvenc = new NvEncoderCuda(*(nv_enc->cuContextPtr), nWidth, nHeight, eInputFormat);
        // NvEncoderCuda enc(cuContext, nWidth, nHeight, eFormat);

        NV_ENC_INITIALIZE_PARAMS initializeParams = {NV_ENC_INITIALIZE_PARAMS_VER};
        NV_ENC_CONFIG encodeConfig = {NV_ENC_CONFIG_VER};
        initializeParams.encodeConfig = &encodeConfig;
        GUID codecId = NV_ENC_CODEC_H264_GUID;
        nv_enc->nvenc->CreateDefaultEncoderParams(&initializeParams, codecId, NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID);

        //pEncodeCLIOptions->SetInitParams(&initializeParams, eFormat);

        nv_enc->nvenc->CreateEncoder(&initializeParams);

        // NvThread thEncode(std::thread(EncodeProc, cuDevice, nWidth, nHeight, eInputFormat, &encodeCLIOptions, bBgra64, szInFilePath, szMediaUri, std::ref(encExceptionPtr)));
        const char *szInFilePath = "/home/lei/myvideos/test_h264_5s_nv12.yuv";
        std::ifstream fpIn(szInFilePath, std::ifstream::in | std::ifstream::binary);
        if (!fpIn)
        {
            std::cout << "Unable to open input file: " << szInFilePath << std::endl;
            return false;
        }

        const char *szOutFilePath = "/home/lei/myvideos/enc_test_0202.h264";
        std::ofstream fpOut(szOutFilePath, std::ios::out | std::ios::binary);

        int nHostFrameSize = nv_enc->nvenc->GetFrameSize();
        std::unique_ptr<uint8_t[]> pHostFrame(new uint8_t[nHostFrameSize]);
        CUdeviceptr dpBgraFrame = 0;
        ck(cuMemAlloc(&dpBgraFrame, nWidth * nHeight * 8));
        int nFrame = 0;
        std::streamsize nRead = 0;
        // FFmpegStreamer streamer(AV_CODEC_ID_H264, nWidth, nHeight, 25, szMediaPath);
        do
        {
            std::vector<std::vector<uint8_t>> vPacket;
            // int val = std::rand() % 2000;
            // usleep(val * 1000);
            nRead = fpIn.read(reinterpret_cast<char *>(pHostFrame.get()), nHostFrameSize).gcount();
            if (nRead == nHostFrameSize)
            {
                const NvEncInputFrame *encoderInputFrame = nv_enc->nvenc->GetNextInputFrame();

                if (true)
                {
                    NvEncoderCuda::CopyToDeviceFrame(*(nv_enc->cuContextPtr), pHostFrame.get(), 0, (CUdeviceptr)encoderInputFrame->inputPtr,
                                                     (int)encoderInputFrame->pitch,
                                                     nv_enc->nvenc->GetEncodeWidth(),
                                                     nv_enc->nvenc->GetEncodeHeight(),
                                                     CU_MEMORYTYPE_HOST,
                                                     encoderInputFrame->bufferFormat,
                                                     encoderInputFrame->chromaOffsets,
                                                     encoderInputFrame->numChromaPlanes);
                }
                nv_enc->nvenc->EncodeFrame(vPacket);
            }
            else
            {
                nv_enc->nvenc->EndEncode(vPacket);
            }
            for (std::vector<uint8_t> &packet : vPacket)
            {
                // streamer.Stream(packet.data(), (int)packet.size(), nFrame++);
                fpOut.write(reinterpret_cast<char *>(packet.data()), (int)packet.size());
                nFrame++;
            }

        } while (nRead == nHostFrameSize);
        ck(cuMemFree(dpBgraFrame));
        dpBgraFrame = 0;

        nv_enc->nvenc->DestroyEncoder();
        fpIn.close();

        std::cout << std::flush << "Total frames encoded: " << nFrame << std::endl
                  << std::flush;
    }
    catch (const std::exception &)
    {
        // std::exception_ptr encExceptionPtr = std::current_exception();
        ck(cuMemFree(dpBgraFrame));
        dpBgraFrame = 0;
        ck(cuMemFree(dpFrame));
        dpFrame = 0;
    }

    return true;
}

// 全流程：从本地存储了yuv像素的文件逐帧读取并编码，再将编码后h264文件保存到本地
// 全流程中的Encode环节调用接口CEncoder.encode()
static bool nvenc_test1(void)
{

    if (!is_supported())
    {
        return false;
    }

    struct nvenc_data *nv_enc = new nvenc_data;

    CUdeviceptr dpFrame = 0, dpBgraFrame = 0;

    try
    {
        ck(cuInit(0));
        // CUdevice cuDevice = 0;
        // nv_enc->cudevicePtr = &cuDevice;
        // // ck(cuDeviceGet(&cuDevice, iGpu));
        // char szDeviceName[80];
        // ck(cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice));
        // std::cout << "GPU in use: " << szDeviceName << std::endl;

        nv_enc->cuContextPtr = new CUcontext();
        ck(cuCtxCreate(nv_enc->cuContextPtr, 0, nv_enc->cuDevice));

        struct CEncoderConfig encoder_config =
            {
                .width = 544,
                .height = 960,
                .framerate = 25,
                .bitrate = 10000,
                .gop = NVENC_INFINITE_GOPLENGTH,
                .codec = "h264",
                .format = NV_ENC_BUFFER_FORMAT_NV12,

            };

        struct CEncoderConfig *config = &encoder_config;

        if (nv_enc->nvenc != nullptr)
        {
            return false;
        }

        nv_enc->width = config->width;
        nv_enc->height = config->height;
        nv_enc->framerate = config->framerate;
        nv_enc->format = config->format;
        nv_enc->codec = config->codec;
        // nv_enc->gop = config->gop;
        nv_enc->bitrate = config->bitrate;

        // NV_ENC_BUFFER_FORMAT eInputFormat = NV_ENC_BUFFER_FORMAT_NV12;
        // int nWidth = 544, nHeight = 960;
        nv_enc->nvenc = new NvEncoderCuda(*(nv_enc->cuContextPtr), nv_enc->width, nv_enc->height, nv_enc->format);

        NV_ENC_INITIALIZE_PARAMS initializeParams = {NV_ENC_INITIALIZE_PARAMS_VER};
        NV_ENC_CONFIG encodeConfig = {NV_ENC_CONFIG_VER};
        initializeParams.encodeConfig = &encodeConfig;

        GUID codecId = NV_ENC_CODEC_H264_GUID;
        if (nv_enc->codec == "h264")
        {
            codecId = NV_ENC_CODEC_H264_GUID;
        }
        else if (nv_enc->codec == "hevc")
        {
            codecId = NV_ENC_CODEC_HEVC_GUID;
        }
        else
        {
            printf("[nvenc] Error: Unsupported codec. \n");
            return false;
        }

        // 默认h264及配置文件
        //  NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID
        nv_enc->nvenc->CreateDefaultEncoderParams(&initializeParams, codecId, NV_ENC_PRESET_DEFAULT_GUID);

        encodeConfig.gopLength = NVENC_INFINITE_GOPLENGTH;
        encodeConfig.frameIntervalP = 1;
        encodeConfig.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;

        encodeConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ;
        encodeConfig.rcParams.averageBitRate = (static_cast<unsigned int>(5.0f * initializeParams.encodeWidth * initializeParams.encodeHeight) / (1280 * 720)) * 100000;
        encodeConfig.rcParams.vbvBufferSize = (encodeConfig.rcParams.averageBitRate * initializeParams.frameRateDen / initializeParams.frameRateNum) * 5;
        encodeConfig.rcParams.maxBitRate = encodeConfig.rcParams.averageBitRate;
        encodeConfig.rcParams.vbvInitialDelay = encodeConfig.rcParams.vbvBufferSize;

        NvEncoderInitParam pEncodeCLIOptions;
        pEncodeCLIOptions.SetInitParams(&initializeParams, nv_enc->format);

        nv_enc->nvenc->CreateEncoder(&initializeParams);

        // NvThread thEncode(std::thread(EncodeProc, cuDevice, nWidth, nHeight, eInputFormat, &encodeCLIOptions, bBgra64, szInFilePath, szMediaUri, std::ref(encExceptionPtr)));
        const char *szInFilePath = "/home/lei/myvideos/test_h264_5s_nv12.yuv";
        std::ifstream fpIn(szInFilePath, std::ifstream::in | std::ifstream::binary);
        if (!fpIn)
        {
            std::cout << "Unable to open input file: " << szInFilePath << std::endl;
            return false;
        }

        const char *szOutFilePath = "/home/lei/myvideos/enc_test_0203.h264";
        std::ofstream fpOut(szOutFilePath, std::ios::out | std::ios::binary);

        int nHostFrameSize = nv_enc->nvenc->GetFrameSize();
        std::unique_ptr<uint8_t[]> pHostFrame(new uint8_t[nHostFrameSize]);

        ck(cuMemAlloc(&(nv_enc->dpBgraFrame), nv_enc->width * nv_enc->height * 8));
        int nFrame = 0;
        std::streamsize nRead = 0;
        int i = 0;
        // FFmpegStreamer streamer(AV_CODEC_ID_H264, nWidth, nHeight, 25, szMediaPath);
        do
        {
            std::vector<std::vector<uint8_t>> vPacket;
            // int val = std::rand() % 20;
            // usleep(val * 1000);
            nRead = fpIn.read(reinterpret_cast<char *>(pHostFrame.get()), nHostFrameSize).gcount();
            if (nRead == nHostFrameSize)
            {
                const NvEncInputFrame *encoderInputFrame = nv_enc->nvenc->GetNextInputFrame();

                ++i;
                if (i && i % 20 == 1)
                {
                    // NV_ENC_RECONFIGURE_PARAMS reconfigureParams = {NV_ENC_RECONFIGURE_PARAMS_VER};
                    // memcpy(&reconfigureParams.reInitEncodeParams, &initializeParams, sizeof(initializeParams));
                    // NV_ENC_CONFIG reInitCodecConfig = {NV_ENC_CONFIG_VER};
                    // memcpy(&reInitCodecConfig, initializeParams.encodeConfig, sizeof(reInitCodecConfig));
                    // reconfigureParams.reInitEncodeParams.encodeConfig = &reInitCodecConfig;

                    NV_ENC_RECONFIGURE_PARAMS reconfigureParams = {NV_ENC_RECONFIGURE_PARAMS_VER};
                    NV_ENC_CONFIG encodeConfig = {NV_ENC_CONFIG_VER};
                    reconfigureParams.forceIDR = true;
                    reconfigureParams.reInitEncodeParams = {NV_ENC_INITIALIZE_PARAMS_VER};
                    reconfigureParams.reInitEncodeParams.encodeConfig = &encodeConfig;
                    nv_enc->nvenc->GetInitializeParams(&reconfigureParams.reInitEncodeParams);

                    // if (i % 40 != 1)
                    {
                        reconfigureParams.reInitEncodeParams.encodeConfig->rcParams.averageBitRate = reconfigureParams.reInitEncodeParams.encodeConfig->rcParams.averageBitRate / 2;
                        // reconfigureParams.reInitEncodeParams.encodeConfig->rcParams.averageBitRate = 20;
                        reconfigureParams.reInitEncodeParams.encodeConfig->rcParams.vbvBufferSize = reconfigureParams.reInitEncodeParams.encodeConfig->rcParams.averageBitRate *
                                                                                                    reconfigureParams.reInitEncodeParams.frameRateDen / reconfigureParams.reInitEncodeParams.frameRateNum;
                        reconfigureParams.reInitEncodeParams.encodeConfig->rcParams.vbvInitialDelay = reconfigureParams.reInitEncodeParams.encodeConfig->rcParams.vbvBufferSize;
                    }
                    nv_enc->nvenc->Reconfigure(&reconfigureParams);
                }

                if (false)
                {
                    NvEncoderCuda::CopyToDeviceFrame(*(nv_enc->cuContextPtr), pHostFrame.get(), 0, (CUdeviceptr)encoderInputFrame->inputPtr,
                                                     (int)encoderInputFrame->pitch,
                                                     nv_enc->nvenc->GetEncodeWidth(),
                                                     nv_enc->nvenc->GetEncodeHeight(),
                                                     CU_MEMORYTYPE_HOST,
                                                     encoderInputFrame->bufferFormat,
                                                     encoderInputFrame->chromaOffsets,
                                                     encoderInputFrame->numChromaPlanes);

                    nv_enc->nvenc->EncodeFrame(vPacket);

                    for (std::vector<uint8_t> &packet : vPacket)
                    {
                        // streamer.Stream(packet.data(), (int)packet.size(), nFrame++);
                        fpOut.write(reinterpret_cast<char *>(packet.data()), (int)packet.size());
                        nFrame++;
                    }
                }
                else
                {

                    int max_buffer_size = nv_enc->height * nv_enc->width * 4;
                    std::shared_ptr<uint8_t> out_buffer(new uint8_t[max_buffer_size]);

                    int frame_size = nv_c_encoder.encode(nv_enc, pHostFrame.get(), out_buffer.get(), max_buffer_size);
                    if (frame_size > 0)
                    {
                        nFrame++;
                        fpOut.write(reinterpret_cast<char *>(out_buffer.get()), frame_size);
                    }
                }
            }
            else
            {
                nv_enc->nvenc->EndEncode(vPacket);
                std::cout << "vPacket !\n";

                for (std::vector<uint8_t> &packet : vPacket)
                {
                    std::cout << "frame: "
                              << nFrame + 1 << std::endl;
                    // streamer.Stream(packet.data(), (int)packet.size(), nFrame++);
                    fpOut.write(reinterpret_cast<char *>(packet.data()), (int)packet.size());
                    nFrame++;
                }
            }

        } while (nRead == nHostFrameSize);
        ck(cuMemFree(nv_enc->dpBgraFrame));
        dpBgraFrame = 0;

        nv_enc->nvenc->DestroyEncoder();
        fpIn.close();

        std::cout << std::flush << "Total frames encoded: " << nFrame << std::endl
                  << std::flush;
    }
    catch (const std::exception &)
    {
        // std::exception_ptr encExceptionPtr = std::current_exception();
        ck(cuMemFree(dpBgraFrame));
        dpBgraFrame = 0;
        ck(cuMemFree(dpFrame));
        dpFrame = 0;
    }

    return true;
}

// 全流程：从本地存储了yuv像素的文件逐帧读取并编码，再将编码后h264文件保存到本地
// 全流程全部调用接口CEncoder中接口
static bool nvenc_test2(void)
{

    if (!is_supported())
    {
        return false;
    }

    try
    {
        void *nv_enc = nv_c_encoder.create();
        struct CEncoderConfig encoder_config =
            {
                .width = 544,
                .height = 960,
                .framerate = 25,
                .bitrate = 100000,
                .gop = 30,
                .codec = "h264",
                .format = NV_ENC_BUFFER_FORMAT_NV12,

            };
        nv_c_encoder.init(nv_enc, &encoder_config);

        // NvThread thEncode(std::thread(EncodeProc, cuDevice, nWidth, nHeight, eInputFormat, &encodeCLIOptions, bBgra64, szInFilePath, szMediaUri, std::ref(encExceptionPtr)));
        const char *szInFilePath = "/home/lei/myvideos/test_h264_5s_nv12.yuv";
        std::ifstream fpIn(szInFilePath, std::ifstream::in | std::ifstream::binary);
        if (!fpIn)
        {
            std::cout << "Unable to open input file: " << szInFilePath << std::endl;
            return false;
        }

        const char *szOutFilePath = "/home/lei/myvideos/enc_test_0203.h264";
        std::ofstream fpOut(szOutFilePath, std::ios::out | std::ios::binary);

        int nHostFrameSize = nv_c_encoder.get_framesize(nv_enc);
        std::unique_ptr<uint8_t[]> pHostFrame(new uint8_t[nHostFrameSize]);

        int nFrame = 0;
        std::streamsize nRead = 0;
        int i = 0;
        // FFmpegStreamer streamer(AV_CODEC_ID_H264, nWidth, nHeight, 25, szMediaPath);
        do
        {
            std::vector<std::vector<uint8_t>> vPacket;
            // int val = std::rand() % 20;
            // usleep(val * 1000);
            nRead = fpIn.read(reinterpret_cast<char *>(pHostFrame.get()), nHostFrameSize).gcount();
            if (nRead == nHostFrameSize)
            {
                // const NvEncInputFrame *encoderInputFrame = nv_enc->nvenc->GetNextInputFrame();

                ++i;
                if (i && i % 20 == 1)
                {
                    nv_c_encoder.set_bitrate(nv_enc, 200000);
                }

                if (true)
                {
                    int max_buffer_size = nv_c_encoder.get_framesize(nv_enc) * 4;
                    std::shared_ptr<uint8_t> out_buffer(new uint8_t[max_buffer_size]);

                    int frame_size = nv_c_encoder.encode(nv_enc, pHostFrame.get(), out_buffer.get(), max_buffer_size);
                    if (frame_size > 0)
                    {
                        nFrame++;
                        fpOut.write(reinterpret_cast<char *>(out_buffer.get()), frame_size);
                    }
                }
            }
            else
            {
                // nv_enc->nvenc->EndEncode(vPacket);
                std::cout << "vPacket !\n";

                // for (std::vector<uint8_t> &packet : vPacket)
                // {
                // 	std::cout << "frame: "
                // 			  << nFrame + 1 << std::endl;
                // 	// streamer.Stream(packet.data(), (int)packet.size(), nFrame++);
                // 	fpOut.write(reinterpret_cast<char *>(packet.data()), (int)packet.size());
                // 	nFrame++;
                // }
            }

        } while (nRead == nHostFrameSize);

        nv_c_encoder.destroy(&nv_enc);
        // nv_enc->nvenc->DestroyEncoder();
        fpIn.close();

        std::cout << std::flush << "Total frames encoded: " << nFrame << std::endl
                  << std::flush;
    }
    catch (const std::exception &e)
    {
        // std::exception_ptr encExceptionPtr = std::current_exception();
        std::cout << e.what();
    }

    return true;
}

struct CEncoder nv_c_encoder = {
    NULL,
    is_supported,
    nvenc_create,
    nvenc_destroy,
    nvenc_init,

    nvenc_set_bitrate,
    nvenc_set_framerate,
    nvenc_request_idr,
    nvenc_get_framesize,

    nvenc_encode,

    nvenc_get_sequence_params,
    nvenc_test2,
};
