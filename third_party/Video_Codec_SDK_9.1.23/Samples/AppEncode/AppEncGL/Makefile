################################################################################
#
# Copyright 2017-2019 NVIDIA Corporation.  All rights reserved.
#
# Please refer to the NVIDIA end user license agreement (EULA) associated
# with this source code for terms and conditions that govern your use of
# this software. Any use, reproduction, disclosure, or distribution of
# this software and related documentation outside the terms of the EULA
# is strictly prohibited.
#
################################################################################

include ../../common.mk

LDFLAGS += -lnvidia-encode

# Target rules
all: build

build: AppEncGL

LDFLAGS += -lX11 -lGL -lglut -lGLEW -lEGL

NvEncoder.o: ../../NvCodec/NvEncoder/NvEncoder.cpp ../../NvCodec/NvEncoder/NvEncoder.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

NvEncoderGL.o: ../../NvCodec/NvEncoder/NvEncoderGL.cpp ../../NvCodec/NvEncoder/NvEncoderGL.h \
               ../../NvCodec/NvEncoder/NvEncoder.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

AppEncGL.o: AppEncGL.cpp GraphicsUtils.cpp ../../NvCodec/NvEncoder/NvEncoderGL.h \
            ../../NvCodec/NvEncoder/NvEncoder.h ../../Utils/NvCodecUtils.h \
            ../../Utils/NvEncoderCLIOptions.h ../../Utils/Logger.h \
            GraphicsUtils.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

GraphicsUtils.o: GraphicsUtils.cpp GraphicsUtils.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

AppEncGL: AppEncGL.o NvEncoder.o NvEncoderGL.o GraphicsUtils.o
	$(GCC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf AppEncGL AppEncGL.o NvEncoder.o NvEncoderGL.o GraphicsUtils.o
