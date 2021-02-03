/*
* Copyright 2017-2018 NVIDIA Corporation.  All rights reserved.
*
* Please refer to the NVIDIA end user license agreement (EULA) associated
* with this source code for terms and conditions that govern your use of
* this software. Any use, reproduction, disclosure, or distribution of
* this software and related documentation outside the terms of the EULA
* is strictly prohibited.
*
*/

#pragma once
#include <iostream>
#include "NvEncoder/NvEncoder.h"
#include "../Utils/NvEncoderCLIOptions.h"

inline void ShowHelpAndExit_AppEncD3D(const char *szBadOption = NULL, bool bOutputInVidMem = false)
{
    bool bThrowError = false;
    std::ostringstream oss;
    if (szBadOption)
    {
        bThrowError = true;
        oss << "Error parsing \"" << szBadOption << "\"" << std::endl;
    }
    oss << "Options:" << std::endl
        << "-i           Input file (must be in BGRA format) path" << std::endl
        << "-o           Output file path" << std::endl
        << "-s           Input resolution in this form: WxH" << std::endl
        << "-gpu         Ordinal of GPU to use" << std::endl
        << "-nv12        (No value) Convert to NV12 before encoding. Don't use it with -444" << std::endl
        ;

    if (bOutputInVidMem)
    {
        oss << "-outputInVidMem  Set this to 1 to enable output in Video Memory" << std::endl;
    }

    oss << NvEncoderInitParam().GetHelpMessage();
    if (bThrowError)
    {
        throw std::invalid_argument(oss.str());
    }
    else
    {
        std::cout << oss.str();
        exit(0);
    }
}

inline void ParseCommandLine_AppEncD3D(int argc, char *argv[], char *szInputFileName, int &nWidth, int &nHeight,
    char *szOutputFileName, NvEncoderInitParam &initParam, int &iGpu, bool &bForceNv12, int *outputInVidMem = NULL, bool bEnableOutputInVidMem = false)
{
    std::ostringstream oss;
    int i;
    for (i = 1; i < argc; i++) {
        if (!_stricmp(argv[i], "-h")) {
            ShowHelpAndExit_AppEncD3D(NULL, bEnableOutputInVidMem);
        }
        if (!_stricmp(argv[i], "-i")) {
            if (++i == argc) {
                ShowHelpAndExit_AppEncD3D("-i", bEnableOutputInVidMem);
            }
            sprintf(szInputFileName, "%s", argv[i]);
            continue;
        }
        if (!_stricmp(argv[i], "-o")) {
            if (++i == argc) {
                ShowHelpAndExit_AppEncD3D("-o", bEnableOutputInVidMem);
            }
            sprintf(szOutputFileName, "%s", argv[i]);
            continue;
        }
        if (!_stricmp(argv[i], "-s")) {
            if (++i == argc || 2 != sscanf(argv[i], "%dx%d", &nWidth, &nHeight)) {
                ShowHelpAndExit_AppEncD3D("-s", bEnableOutputInVidMem);
            }
            continue;
        }
        if (!_stricmp(argv[i], "-gpu")) {
            if (++i == argc) {
                ShowHelpAndExit_AppEncD3D("-gpu", bEnableOutputInVidMem);
            }
            iGpu = atoi(argv[i]);
            continue;
        }
        if (!_stricmp(argv[i], "-nv12")) {
            bForceNv12 = true;
            continue;
        }
        if (!_stricmp(argv[i], "-outputInVidMem"))
        {
            if (++i == argc)
            {
                ShowHelpAndExit_AppEncD3D("-outputInVidMem", bEnableOutputInVidMem);
            }
            if (outputInVidMem != NULL)
            {
                *outputInVidMem = (atoi(argv[i]) != 0) ? 1 : 0;
            }
            continue;
        }

        // Regard as encoder parameter
        if (argv[i][0] != '-') {
            ShowHelpAndExit_AppEncD3D(argv[i], bEnableOutputInVidMem);
        }
        oss << argv[i] << " ";
        while (i + 1 < argc && argv[i + 1][0] != '-') {
            oss << argv[++i] << " ";
        }
    }
    initParam = NvEncoderInitParam(oss.str().c_str());
}
