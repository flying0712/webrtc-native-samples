

GCC ?= g++

CCFLAGS := -std=c++14  -DWEBRTC_LINUX -DWEBRTC_POSIX  -DWEBRTC_USE_H264 -fno-rtti -lpthread -ldl

# Debug build flags
ifeq ($(dbg),1)
    CCFLAGS += -g
endif

NVDIA_SDK_PATH := ../../third_party/Video_Codec_SDK_9.1.23
NVDIA_SDK_SAMPLES_PATH := ${NVDIA_SDK_PATH}/Samples

CUDA_PATH ?= /usr/local/cuda

# Link applications against stub libraries provided in the SDKs.
LDFLAGS := -L$(CUDA_PATH)/lib64/stubs
LDFLAGS += -L${NVDIA_SDK_PATH}/Lib/linux/stubs/$(shell uname -p)
LDFLAGS += -ldl -lcuda

NVCC ?= $(CUDA_PATH)/bin/nvcc

# Common includes and paths
INCLUDES := -I$(CUDA_PATH)/include
INCLUDES += -I${NVDIA_SDK_PATH}/include
# third_party/Video_Codec_SDK_9.1.23/include
# INCLUDES += -I${NVDIA_SDK_SAMPLES_PATH}/NvCodec
# INCLUDES += -I${NVDIA_SDK_SAMPLES_PATH}/NvCodec/include
# INCLUDES += -I${NVDIA_SDK_SAMPLES_PATH}/NvCodec/NvDecoder
# INCLUDES += -I${NVDIA_SDK_SAMPLES_PATH}/NvCodec/NvEncoder
#INCLUDES += -I${NVDIA_SDK_SAMPLES_PATH}/NvCodec/Common
INCLUDES += -I${NVDIA_SDK_SAMPLES_PATH}


NVCCFLAGS := $(CCFLAGS)

LDFLAGS += -pthread
LDFLAGS += -lnvcuvid -L$(CUDA_PATH)/lib64 -lcudart -lnvidia-encode
# LDFLAGS += $(shell pkg-config --libs libavcodec libavutil libavformat)

# INCLUDES += $(shell pkg-config --cflags libavcodec libavutil libavformat)

# INCLUDES += ${NVDIA_SDK_SAMPLES_PATH} ${NVDIA_SDK_SAMPLES_PATH}/NvCodec


WEBRTC_PATH := ../../third_party/webrtc
INCLUDES += -I${WEBRTC_PATH}/include
INCLUDES += -I${WEBRTC_PATH}/include/third_party/abseil-cpp/
INCLUDES += -I${WEBRTC_PATH}/include/third_party/libyuv/include



# Target rules
all: build

build: test_nv_encoder


NvEncoder.o: ${NVDIA_SDK_SAMPLES_PATH}/NvCodec/NvEncoder/NvEncoder.cpp# ${NVDIA_SDK_SAMPLES_PATH}/NvCodec/NvEncoder/NvEncoder.h
	echo $(INCLUDES)
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

NvEncoderCuda.o: ${NVDIA_SDK_SAMPLES_PATH}/NvCodec/NvEncoder/NvEncoderCuda.cpp# ${NVDIA_SDK_SAMPLES_PATH}/NvCodec/NvEncoder/NvEncoderCuda.h \
                 ${NVDIA_SDK_SAMPLES_PATH}/NvCodec/NvEncoder/NvEncoder.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

c_encoder_nv_impl.o: c_encoder_nv_impl.cpp
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

h264_encoder_adaptor.o: h264_encoder_adaptor.cpp
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<



test_nv_encoder.o: test_nv_encoder.cpp 
#	$(GCC)  $(CCFLAGS) $(INCLUDES) -E  $<
	$(GCC)  $(CCFLAGS) $(INCLUDES) -o $@  $<




test_nv_encoder: test_nv_encoder.o c_encoder_nv_impl.o NvEncoderCuda.o NvEncoder.o
	$(GCC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)




clean:
	rm -rf test_nv_encoder test_nv_encoder.o c_encoder_nv_impl.o NvEncoderCuda.o NvEncoder.o
