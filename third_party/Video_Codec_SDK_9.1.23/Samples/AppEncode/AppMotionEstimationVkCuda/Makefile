################################################################################
#
# Copyright 2018 NVIDIA Corporation.  All rights reserved.
#
# Please refer to the NVIDIA end user license agreement (EULA) associated
# with this source code for terms and conditions that govern your use of
# this software. Any use, reproduction, disclosure, or distribution of
# this software and related documentation outside the terms of the EULA
# is strictly prohibited.
#
################################################################################

include ../../common.mk

CCFLAGS += $(shell pkg-config --cflags vulkan)

LDFLAGS += -lnvidia-encode
LDFLAGS += $(shell pkg-config --libs vulkan)

# Target rules
all: build

build: AppMotionEstimationVkCuda


AppMotionEstimationVkCuda: AppMotionEstimationVkCuda.o utility.o NvEncoder.o NvEnc.o
	$(GCC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)

AppMotionEstimationVkCuda.o: AppMotionEstimationVkCuda.cpp utility.h \
        ../../NvCodec/NvEncoder/NvEncoder.h NvEnc.h \
        ../../Utils/NvCodecUtils.h ../../Utils/NvEncoderCLIOptions.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

utility.o: utility.cpp utility.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

NvEncoder.o: ../../NvCodec/NvEncoder/NvEncoder.cpp ../../NvCodec/NvEncoder/NvEncoder.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

NvEnc.o: NvEnc.cpp NvEnc.h ../../NvCodec/NvEncoder/NvEncoder.h
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

clean:
	rm -f AppMotionEstimationVkCuda AppMotionEstimationVkCuda.o \
	      utility.o NvEncoder.o NvEnc.o
