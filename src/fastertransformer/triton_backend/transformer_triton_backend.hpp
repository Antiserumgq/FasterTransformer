/*
 * Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <memory>
#include <sstream>
#include <sys/time.h>
#include <vector>

#include "src/fastertransformer/utils/Tensor.h"
#include "src/fastertransformer/utils/nccl_utils.h"

namespace ft = fastertransformer;

namespace triton {
#ifdef USE_TRITONSERVER_DATATYPE

#include "triton/core/tritonbackend.h"
#include "triton/core/tritonserver.h"
typedef TRITONSERVER_DataType DataType;
typedef TRITONSERVER_MemoryType MemoryType;

constexpr TRITONSERVER_DataType TYPE_INVALID = TRITONSERVER_TYPE_INVALID;
constexpr TRITONSERVER_DataType TYPE_BOOL = TRITONSERVER_TYPE_BOOL;
constexpr TRITONSERVER_DataType TYPE_UINT8 = TRITONSERVER_TYPE_UINT8;
constexpr TRITONSERVER_DataType TYPE_UINT16 = TRITONSERVER_TYPE_UINT16;
constexpr TRITONSERVER_DataType TYPE_UINT32 = TRITONSERVER_TYPE_UINT32;
constexpr TRITONSERVER_DataType TYPE_UINT64 = TRITONSERVER_TYPE_UINT64;
constexpr TRITONSERVER_DataType TYPE_INT8 = TRITONSERVER_TYPE_INT8;
constexpr TRITONSERVER_DataType TYPE_INT16 = TRITONSERVER_TYPE_INT16;
constexpr TRITONSERVER_DataType TYPE_INT32 = TRITONSERVER_TYPE_INT32;
constexpr TRITONSERVER_DataType TYPE_INT64 = TRITONSERVER_TYPE_INT64;
constexpr TRITONSERVER_DataType TYPE_FP16 = TRITONSERVER_TYPE_FP16;
constexpr TRITONSERVER_DataType TYPE_FP32 = TRITONSERVER_TYPE_FP32;
constexpr TRITONSERVER_DataType TYPE_FP64 = TRITONSERVER_TYPE_FP64;
constexpr TRITONSERVER_DataType TYPE_BYTES = TRITONSERVER_TYPE_BYTES;
constexpr TRITONSERVER_MemoryType MEMORY_CPU = TRITONSERVER_MEMORY_CPU;
constexpr TRITONSERVER_MemoryType MEMORY_CPU_PINNED = TRITONSERVER_MEMORY_CPU_PINNED;
constexpr TRITONSERVER_MemoryType MEMORY_GPU = TRITONSERVER_MEMORY_GPU;

#else

typedef ft::DataType DataType;
typedef ft::MemoryType MemoryType;

constexpr DataType TYPE_INVALID = ft::TYPE_INVALID;
constexpr DataType TYPE_BOOL = ft::TYPE_BOOL;
constexpr DataType TYPE_UINT8 = ft::TYPE_UINT8;
constexpr DataType TYPE_UINT16 = ft::TYPE_UINT16;
constexpr DataType TYPE_UINT32 = ft::TYPE_UINT32;
constexpr DataType TYPE_UINT64 = ft::TYPE_UINT64;
constexpr DataType TYPE_INT8 = ft::TYPE_INT8;
constexpr DataType TYPE_INT16 = ft::TYPE_INT16;
constexpr DataType TYPE_INT32 = ft::TYPE_INT32;
constexpr DataType TYPE_INT64 = ft::TYPE_INT64;
constexpr DataType TYPE_FP16 = ft::TYPE_FP16;
constexpr DataType TYPE_FP32 = ft::TYPE_FP32;
constexpr DataType TYPE_FP64 = ft::TYPE_FP64;
constexpr DataType TYPE_BYTES = ft::TYPE_BYTES;
constexpr MemoryType MEMORY_CPU = ft::MEMORY_CPU;
constexpr MemoryType MEMORY_CPU_PINNED = ft::MEMORY_CPU_PINNED;
constexpr MemoryType MEMORY_GPU = ft::MEMORY_GPU;

#endif

struct Tensor {
    const MemoryType where;
    const DataType type;
    const std::vector<size_t> shape;
    const void* data;

    Tensor(const MemoryType _where, const DataType _type, const std::vector<size_t> _shape, const void* _data):
        where(_where), type(_type), shape(_shape), data(_data)
    {
    }
};

}  // namespace triton

struct AbstractTransformerModel;
struct AbstractTransformerModelInstance;

struct AbstractTransformerModelInstance {
    virtual std::shared_ptr<std::vector<triton::Tensor>>
    forward(std::shared_ptr<std::vector<triton::Tensor>> input_tensors) = 0;
};

struct AbstractTransformerModel {
    static std::shared_ptr<AbstractTransformerModel> createGptModel(std::string inifile);
    static std::shared_ptr<AbstractTransformerModel> createGptJModel(std::string inifile);
    virtual std::vector<ncclUniqueId> createNcclIds(const uint32_t world_size) = 0;

    virtual std::pair<std::vector<ncclComm_t>, std::vector<ncclComm_t>>
    createNcclComms(std::vector<ncclUniqueId> nccl_ids, const int node_id) = 0;

    virtual std::unique_ptr<AbstractTransformerModelInstance>
    createModelInstance(int deviceId,
                        int rank,
                        cudaStream_t stream,
                        std::pair<std::vector<ncclComm_t>, std::vector<ncclComm_t>> nccl_comms) = 0;

    virtual std::string toString() = 0;
    virtual std::pair<uint32_t, uint32_t> getMaxBatchSeqlen() = 0;
    virtual int getTensorParaSize() = 0;
    virtual int getPipelineParaSize() = 0;
};
