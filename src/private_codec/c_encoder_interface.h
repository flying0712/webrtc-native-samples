
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// TODO: 定义不依赖nvcode的像素格式
#include "nvEncodeAPI.h"

struct CEncoderConfig
{
    uint32_t width;
    uint32_t height;
    uint32_t framerate;
    uint32_t bitrate;
    uint32_t gop;
    std::string codec; // "h264"

    // TODO: 定义不依赖nvcode的像素格式
    NV_ENC_BUFFER_FORMAT format; // NV_ENC_BUFFER_FORMAT_NV12
};

struct CEncoder
{
    void *data;

    bool (*is_supported)(void);
    void *(*create)(void);
    void (*destroy)(void **encoder_data);
    bool (*init)(void *encoder_data, void *encoder_config);

    int (*set_bitrate)(void *nvenc_data, uint32_t bitrate_bps);
    int (*set_framerate)(void *nvenc_data, uint32_t framerate);
    int (*request_idr)(void *nvenc_data);
    int (*get_framesize)(void *nvenc_data);

    int (*encode)(void *nvenc_data, uint8_t *in_buf, uint8_t *out_buf, uint32_t max_buf_size);

    int (*get_sequence_params)(void *nvenc_data, uint8_t *buf, uint32_t max_buf_size);

    bool (*test)(void);
};
