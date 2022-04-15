/*
 * Copyright (c) 2019-2022, NVIDIA CORPORATION.  All rights reserved.
 * Copyright (c) 2021, NAVER Corp.  Authored by CLOVA.
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

#include "src/fastertransformer/layers/sampling_layers/BaseSamplingLayer.h"

namespace fastertransformer {

template<typename T>
class TopPSamplingLayer: public BaseSamplingLayer<T> {
private:
    void runSampling(std::vector<fastertransformer::Tensor>* output_tensors,
                     const std::vector<fastertransformer::Tensor>* input_tensors) override;
    void runSampling(std::unordered_map<std::string, Tensor>* output_tensors,
                     const std::unordered_map<std::string, Tensor>* input_tensors) override;

    void allocateBuffer() override;
    void allocateBuffer(size_t batch_size, size_t top_k, float top_p) override;
    void freeBuffer() override;
    void invokeInitialize(size_t batch_size, unsigned long long random_seed, curandState_t* curandstate_buf) override;

    int* topp_id_vals_buf_;
    int* topp_offset_buf_;
    int* begin_topp_offset_buf_;
    size_t cub_temp_storage_size_;

    using BaseSamplingLayer<T>::vocab_size_;
    using BaseSamplingLayer<T>::vocab_size_padded_;

    using BaseSamplingLayer<T>::sampling_workspace_size_;
    using BaseSamplingLayer<T>::sampling_workspace_;
    using BaseSamplingLayer<T>::curandstate_buf_;

    using BaseSamplingLayer<T>::stream_;
    using BaseSamplingLayer<T>::allocator_;
    using BaseSamplingLayer<T>::is_allocate_buffer_;
    using BaseSamplingLayer<T>::cuda_device_prop_;

protected:
public:
    TopPSamplingLayer(size_t max_batch_size,
                      size_t vocab_size,
                      size_t vocab_size_padded,
                      int end_id,
                      float top_p,
                      unsigned long long random_seed,
                      float temperature,
                      float len_penalty,
                      float repetition_penalty,
                      cudaStream_t stream,
                      cublasMMWrapper* cublas_wrapper,
                      IAllocator* allocator,
                      bool is_free_buffer_after_forward,
                      cudaDeviceProp* cuda_device_prop);

    TopPSamplingLayer(TopPSamplingLayer<T> const& top_p_sampling_layer);

    ~TopPSamplingLayer();
};

}  // namespace fastertransformer
