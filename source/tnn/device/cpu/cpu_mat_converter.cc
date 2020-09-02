// Tencent is pleased to support the open source community by making TNN available.
//
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "tnn/device/cpu/cpu_mat_converter.h"

#include <algorithm>
#include <cstring>

#include "tnn/core/blob_int8.h"
#include "tnn/core/macro.h"
#include "tnn/utils/naive_compute.h"
#include "tnn/utils/bfp16.h"
#include "tnn/utils/bfp16_utils.h"
#include "tnn/utils/dims_vector_utils.h"

namespace TNN_NS {

CpuMatConverterAcc::CpuMatConverterAcc() : MatConverterAcc() {}
CpuMatConverterAcc::~CpuMatConverterAcc() {}
Status CpuMatConverterAcc::Copy(Mat& src, Mat& dst, void* command_queue) {
    Status ret            = TNN_OK;
    //memcpy(dst.GetData(),src.GetData());
    MatType mat_type   = src.GetMatType();
    int data_type_size = 1;
    DimsVector dims    = src.GetDims();
    if (mat_type == NCHW_FLOAT) {
        data_type_size = sizeof(float);
    } else if (mat_type == N8UC4) {
        //special for 8UC4, blob channel <= 4.
        dims[1] = 4;
    }
    int size_in_bytes = DimsVectorUtils::Count(dims) * data_type_size;
    memcpy(dst.GetData(), src.GetData(), size_in_bytes);
    return ret; 
}

Status CpuMatConverterAcc::Resize(Mat& src, Mat& dst, ResizeParam param, void* command_queue) {
    Status ret            = TNN_OK;
    if (src.GetData() == nullptr) {
        return Status(TNNERR_NULL_PARAM, "input mat is null");
    }

    if (src.GetDeviceType() != dst.GetDeviceType()) {
        return Status(TNNERR_PARAM_ERR, "src and dst mat type must be same");
    }

    if (dst.GetData() == nullptr) {
        dst = Mat(dst.GetDeviceType(), dst.GetMatType(), dst.GetDims());
    }

    if (src.GetMatType() == NCHW_FLOAT) {
        ret = Status(TNNERR_PARAM_ERR, "convert type not support yet");
    } else if (src.GetMatType() == N8UC4) {
        int channel = src.GetChannel();
        if (param.type == INTERP_TYPE_LINEAR) {
            for (int batch = 0; batch < src.GetBatch(); batch++)
            {
                uint8_t* src_ptr = (uint8_t*)src.GetData() + batch * src.GetWidth() * src.GetHeight() * channel;
                uint8_t* dst_ptr = (uint8_t*)dst.GetData() + batch * dst.GetWidth() * dst.GetHeight() * channel;
                resize_bilinear(src_ptr, src.GetWidth(), src.GetHeight(),
                               dst_ptr, dst.GetWidth(), dst.GetHeight());
            } 
        } else {
            return Status(TNNERR_PARAM_ERR, "interpolation type not support yet");
        }
    } else if (src.GetMatType() == N8UC3) {
        ret = Status(TNNERR_PARAM_ERR, "convert type not support yet");
    } else if (src.GetMatType() == NGRAY) {
        ret = Status(TNNERR_PARAM_ERR, "convert type not support yet");
    } else if (src.GetMatType() == RESERVED_BFP16_TEST) {
        ret = Status(TNNERR_PARAM_ERR, "convert type not support yet");
    } else {
        ret = Status(TNNERR_PARAM_ERR, "convert type not support yet");
    }
    return ret;
}

Status CpuMatConverterAcc::Crop(Mat& src, Mat& dst, CropParam param, void* command_queue) {
    Status ret            = TNN_OK;
    if (src.GetData() == nullptr) {
        return Status(TNNERR_NULL_PARAM, "input mat is null");
    }

    if (src.GetDeviceType() != dst.GetDeviceType()) {
        return Status(TNNERR_PARAM_ERR, "src and dst mat type must be same");
    }

    if (dst.GetData() == nullptr) {
        dst = Mat(dst.GetDeviceType(), dst.GetMatType(), dst.GetDims());
    }

    if (src.GetMatType() == NGRAY) {
        // element size 1
        auto src_ptr = GET_OFFSET_PTR(src.GetData(), param.top_left_x + param.top_left_y * src.GetWidth());
        auto dst_ptr = GET_OFFSET_PTR(dst.GetData(), 0);
        MatMemcpy_2d(src_ptr, dst_ptr, param.width, param.height, src.GetWidth(), dst.GetWidth());
    } else if (src.GetMatType() == N8UC3) {
        // element size 3
        auto src_ptr = GET_OFFSET_PTR(src.GetData(), (param.top_left_x + param.top_left_y * src.GetWidth()) * 3);
        auto dst_ptr = GET_OFFSET_PTR(dst.GetData(), 0);
        MatMemcpy_2d(src_ptr, dst_ptr, param.width * 3, param.height, src.GetWidth() * 3, dst.GetWidth() * 3);
    } else if (src.GetMatType() == N8UC4) {
        // element size 4
        int channel = 4;
        for (int batch = 0; batch < src.GetBatch(); batch++)
        {
            auto src_ptr = GET_OFFSET_PTR((uint8_t*)src.GetData() + batch * src.GetWidth() * src.GetHeight() * channel, (param.top_left_x + param.top_left_y * src.GetWidth()) * channel);
            auto dst_ptr = GET_OFFSET_PTR((uint8_t*)dst.GetData() + batch * dst.GetWidth() * dst.GetHeight() * channel, 0);
            MatMemcpy_2d(src_ptr, dst_ptr, param.width * channel, param.height, src.GetWidth() * channel, dst.GetWidth() * channel);
        } 
    } else if (src.GetMatType() == NNV21 || src.GetMatType() == NNV12) {
        if (param.top_left_x % 2 || param.top_left_y % 2 || param.width % 2 || param.height % 2) {
            return Status(TNNERR_PARAM_ERR, "corp param can not be odd");
        }
        // crop y
        auto src_ptr = GET_OFFSET_PTR(src.GetData(), param.top_left_x + param.top_left_y * param.width);
        auto dst_ptr = GET_OFFSET_PTR(dst.GetData(), 0);
        MatMemcpy_2d(src_ptr, dst_ptr, param.width, param.height, src.GetWidth(), dst.GetWidth());
        // crop uv
        src_ptr = GET_OFFSET_PTR(
            src.GetData(), src.GetWidth() * src.GetHeight() + param.top_left_x + param.top_left_y * src.GetWidth() / 2);
        dst_ptr = GET_OFFSET_PTR(dst.GetData(), dst.GetWidth() * dst.GetHeight());
        MatMemcpy_2d(src_ptr, dst_ptr, param.width, param.height / 2, src.GetWidth(), dst.GetWidth());
    } else {
        return Status(TNNERR_PARAM_ERR, "convert type not support yet");
    }

    return ret;
}

Status CpuMatConverterAcc::WarpAffine(Mat& src, Mat& dst, WarpAffineParam param, void* command_queue) {
    //LOGE("cpu mat converter warp affine start, mat type: %d, interp type: %d\n", src.GetMatType(), param.interp_type);
    Status ret            = TNN_OK;
    if (src.GetData() == nullptr) {
        return Status(TNNERR_NULL_PARAM, "input mat is null");
    }

    if (src.GetDeviceType() != dst.GetDeviceType()) {
        return Status(TNNERR_PARAM_ERR, "src and dst mat type must be same");
    }

    if (dst.GetData() == nullptr) {
        dst = Mat(dst.GetDeviceType(), dst.GetMatType(), dst.GetDims());
    }

    if (src.GetMatType() == NGRAY) {
        if (param.interp_type == INTERP_TYPE_LINEAR && param.border_type == BORDER_TYPE_CONSTANT) {
            int channel = 1;
            for (int batch = 0; batch < src.GetDims()[0]; batch++)
            {
                uint8_t* src_ptr = (uint8_t*)src.GetData() + batch * src.GetWidth() * src.GetHeight() * channel;
                uint8_t* dst_ptr = (uint8_t*)dst.GetData() + batch * dst.GetWidth() * dst.GetHeight() * channel;
                warpaffine_bilinear(src_ptr, src.GetWidth(), src.GetHeight(), channel,
                                    dst_ptr, dst.GetWidth(), dst.GetHeight(),
                                    param.transform, param.border_val);
            }
        } else {
            return Status(TNNERR_PARAM_ERR, "warpaffine type not support yet");
        }
    } else if (src.GetMatType() == N8UC3) {
        if (param.interp_type == INTERP_TYPE_LINEAR && param.border_type == BORDER_TYPE_CONSTANT) {
            int channel = 3;
            for (int batch = 0; batch < src.GetDims()[0]; batch++)
            {
                uint8_t* src_ptr = (uint8_t*)src.GetData() + batch * src.GetWidth() * src.GetHeight() * channel;
                uint8_t* dst_ptr = (uint8_t*)dst.GetData() + batch * dst.GetWidth() * dst.GetHeight() * channel;
                warpaffine_bilinear(src_ptr, src.GetWidth(), src.GetHeight(), channel,
                                    dst_ptr, dst.GetWidth(), dst.GetHeight(),
                                    param.transform, param.border_val);
            }
        } else {
            return Status(TNNERR_PARAM_ERR, "warpaffine type not support yet");
        }
    } else if (src.GetMatType() == N8UC4) {
        if (param.interp_type == INTERP_TYPE_LINEAR && param.border_type == BORDER_TYPE_CONSTANT) {
            int channel = 4;
            for (int batch = 0; batch < src.GetDims()[0]; batch++)
            {
                uint8_t* src_ptr = (uint8_t*)src.GetData() + batch * src.GetWidth() * src.GetHeight() * channel;
                uint8_t* dst_ptr = (uint8_t*)dst.GetData() + batch * dst.GetWidth() * dst.GetHeight() * channel;
                warpaffine_bilinear(src_ptr, src.GetWidth(), src.GetHeight(), channel,
                                    dst_ptr, dst.GetWidth(), dst.GetHeight(),
                                    param.transform, param.border_val);
            }
        } else {
            return Status(TNNERR_PARAM_ERR, "warpaffine type not support yet");
        }
    } else {
        return Status(TNNERR_PARAM_ERR, "convert type not support yet");
    }

    return ret;
}

Status CpuMatConverterAcc::BGR2Gray(Mat& src, Mat& dst, void* command_queue) {
    return TNN_OK;
}

void CpuMatConverterAcc::MatMemcpy_2d(void* src, void* dst, int width, int height, int src_stride, int dst_stride) {
    auto src_ptr = reinterpret_cast<uint8_t*>(src);
    auto dst_ptr = reinterpret_cast<uint8_t*>(dst);

    for (int h = 0; h < height; h++) {
        memcpy(dst_ptr, src_ptr, width);
        src_ptr += src_stride;
        dst_ptr += dst_stride;
    }

}

DECLARE_MAT_CONVERTER_CREATER(Cpu);
REGISTER_MAT_CONVERTER(Cpu, DEVICE_NAIVE);

}
