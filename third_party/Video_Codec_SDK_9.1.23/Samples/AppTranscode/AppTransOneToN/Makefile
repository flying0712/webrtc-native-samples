################################################################################
#
# Copyright 2017-2018 NVIDIA Corporation.  All rights reserved.
#
# Please refer to the NVIDIA end user license agreement (EULA) associated
# with this source code for terms and conditions that govern your use of
# this software. Any use, reproduction, disclosure, or distribution of
# this software and related documentation outside the terms of the EULA
# is strictly prohibited.
#
################################################################################

include ../../common.mk

NVCCFLAGS := $(CCFLAGS)

LDFLAGS += -pthread
LDFLAGS += -lnvcuvid -L$(CUDA_PATH)/lib64 -lcudart -lnvidia-encode
LDFLAGS += $(shell pkg-config --libs libavcodec libavutil libavformat)

INCLUDES += $(shell pkg-config --cflags libavcodec libavutil libavformat)

# Target rules
all: build

build: AppTransOneToN

NvDecoder.o: ../../NvCodec/NvDecoder/NvDecoder.cpp ../../NvCodec/NvDecoder/NvDecoder.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

NvEncoder.o: ../../NvCodec/NvEncoder/NvEncoder.cpp ../../NvCodec/NvEncoder/NvEncoder.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

NvEncoderCuda.o: ../../NvCodec/NvEncoder/NvEncoderCuda.cpp ../../NvCodec/NvEncoder/NvEncoderCuda.h \
                 ../../NvCodec/NvEncoder/NvEncoder.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

Resize.o: ../../Utils/Resize.cu
	$(NVCC) $(NVCCFLAGS) $(INCLUDES) -o $@ -c $<

AppTransOneToN.o: AppTransOneToN.cpp ../../NvCodec/NvDecoder/NvDecoder.h \
                  ../../NvCodec/NvEncoder/NvEncoder.h ../../NvCodec/NvEncoder/NvEncoderCuda.h \
                  ../../Utils/NvCodecUtils.h ../../Utils/NvEncoderCLIOptions.h \
                  ../../Utils/Logger.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

AppTransOneToN: AppTransOneToN.o Resize.o NvDecoder.o NvEncoder.o NvEncoderCuda.o
	$(GCC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf AppTransOneToN AppTransOneToN.o Resize.o NvDecoder.o NvEncoderCuda.o NvEncoder.o
