/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
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

#include "src/fastertransformer/models/t5/T5Encoder.h"
#include "src/fastertransformer/kernels/add_residual_kernels.h"
#include "src/fastertransformer/kernels/decoding_kernels.h"

namespace fastertransformer {

template<typename T>
void T5Encoder<T>::initialize()
{
    if ((attention_type_ == AttentionType::FUSED_MHA || attention_type_ == AttentionType::FUSED_PADDED_MHA)
        && false  // disable the fused mha
        && std::is_same<T, half>::value == true && max_seq_len_ <= 384) {
        FT_CHECK(false);  // fused mha does not support relatvie attention bias now.
        // attention_layer_ = new FusedAttentionLayer<T>(max_batch_size_,
        //                                               max_seq_len_,
        //                                               head_num_,
        //                                               size_per_head_,
        //                                               sm_,
        //                                               q_scaling_ * (1.0 / sqrtf(size_per_head_ * 1.0f)),
        //                                               stream_,
        //                                               cublas_wrapper_,
        //                                               allocator_,
        //                                               is_free_buffer_after_forward_,
        //                                               sparse_);
    }
    else if (attention_type_ == AttentionType::UNFUSED_MHA || attention_type_ == AttentionType::UNFUSED_PADDED_MHA) {
        attention_layer_ = new TensorParallelUnfusedAttentionLayer<T>(max_batch_size_,
                                                                      max_seq_len_,
                                                                      head_num_,
                                                                      size_per_head_,
                                                                      d_model_,
                                                                      q_scaling_ * (1.0 / sqrtf(size_per_head_ * 1.0f)),
                                                                      tensor_para_.world_size_,
                                                                      tensor_para_.nccl_comm_,
                                                                      stream_,
                                                                      cublas_wrapper_,
                                                                      allocator_,
                                                                      is_free_buffer_after_forward_,
                                                                      sparse_);
    }
    else {
        throw std::runtime_error(std::string("[FT][ERROR] Invalid attention type \n"));
    }

    if (activation_type_ == ActivationType::Gelu) {
        ffn_layer_ = new TensorParallelGeluFfnLayer<T>(max_batch_size_,
                                                       max_seq_len_,
                                                       1,
                                                       d_model_,
                                                       inter_size_,
                                                       tensor_para_.world_size_,
                                                       tensor_para_.nccl_comm_,
                                                       stream_,
                                                       cublas_wrapper_,
                                                       allocator_,
                                                       is_free_buffer_after_forward_,
                                                       sparse_);
    }
    else if (activation_type_ == ActivationType::Relu) {
        ffn_layer_ = new TensorParallelReluFfnLayer<T>(max_batch_size_,
                                                       max_seq_len_,
                                                       1,
                                                       d_model_,
                                                       inter_size_,
                                                       tensor_para_.world_size_,
                                                       tensor_para_.nccl_comm_,
                                                       stream_,
                                                       cublas_wrapper_,
                                                       allocator_,
                                                       is_free_buffer_after_forward_,
                                                       sparse_);
    }
}

template<typename T>
T5Encoder<T>::T5Encoder(size_t max_batch_size,
                        size_t max_seq_len,
                        size_t head_num,
                        size_t size_per_head,
                        size_t inter_size,
                        size_t d_model,
                        size_t num_layer,
                        size_t num_bucket,
                        size_t max_distance,
                        int sm,
                        float q_scaling,
                        cudaStream_t stream,
                        cublasMMWrapper* cublas_wrapper,
                        IAllocator* allocator,
                        bool is_free_buffer_after_forward,
                        AttentionType attention_type,
                        bool sparse,
                        ActivationType activation_type,
                        LayerNormType layernorm_type,
                        NcclParam tensor_para,
                        NcclParam pipeline_para):
    BaseLayer(stream, cublas_wrapper, allocator, is_free_buffer_after_forward),
    max_batch_size_(max_batch_size),
    max_seq_len_(max_seq_len),
    head_num_(head_num),
    size_per_head_(size_per_head),
    inter_size_(inter_size),
    d_model_(d_model),
    hidden_units_(head_num_ * size_per_head_),
    num_layer_(num_layer),
    num_bucket_(num_bucket),
    max_distance_(max_distance),
    sm_(sm),
    q_scaling_(q_scaling),
    attention_type_(attention_type),
    sparse_(sparse),
    activation_type_(activation_type),
    layernorm_type_(layernorm_type),
    tensor_para_(tensor_para),
    pipeline_para_(pipeline_para)
{
    initialize();
}

template<typename T>
T5Encoder<T>::T5Encoder(T5Encoder<T> const& t5_encoder):
    BaseLayer(t5_encoder),
    max_batch_size_(t5_encoder.max_batch_size_),
    max_seq_len_(t5_encoder.max_seq_len_),
    head_num_(t5_encoder.head_num_),
    size_per_head_(t5_encoder.size_per_head_),
    inter_size_(t5_encoder.inter_size_),
    d_model_(t5_encoder.d_model_),
    hidden_units_(t5_encoder.hidden_units_),
    num_layer_(t5_encoder.num_layer_),
    num_bucket_(t5_encoder.num_bucket_),
    max_distance_(t5_encoder.max_distance_),
    sm_(t5_encoder.sm_),
    q_scaling_(t5_encoder.q_scaling_),
    attention_type_(t5_encoder.attention_type_),
    sparse_(t5_encoder.sparse_),
    activation_type_(t5_encoder.activation_type_),
    layernorm_type_(t5_encoder.layernorm_type_),
    tensor_para_(t5_encoder.tensor_para_),
    pipeline_para_(t5_encoder.pipeline_para_)
{
    initialize();
}

template<typename T>
T5Encoder<T>::~T5Encoder()
{
    delete attention_layer_;
    delete ffn_layer_;
    freeBuffer();
}

template<typename T>
void T5Encoder<T>::allocateBuffer()
{
    if (is_allocate_buffer_ == false) {
        token_num_ = (size_t*)allocator_->malloc(sizeof(size_t) * 1, false);
        padding_offset_ = (int*)allocator_->malloc(sizeof(int) * max_batch_size_ * max_seq_len_, false);
        trt_mha_padding_offset_ = (int*)allocator_->malloc(sizeof(int) * (2 * max_batch_size_ + 1), false);

        attention_mask_ = (T*)allocator_->malloc(sizeof(T) * max_batch_size_ * max_seq_len_ * max_seq_len_, false);
        relative_attention_bias_ = (T*)allocator_->malloc(sizeof(T) * head_num_ * max_seq_len_ * max_seq_len_, false);

        t5_encoder_emb_buf_ = (T*)allocator_->malloc(sizeof(T) * max_batch_size_ * max_seq_len_ * d_model_, false);
        t5_encoder_in_buffer_ = (T*)allocator_->malloc(sizeof(T) * max_batch_size_ * max_seq_len_ * d_model_, false);
        attn_out_buf_ = (T*)allocator_->malloc(sizeof(T) * max_batch_size_ * max_seq_len_ * d_model_, false);
        t5_encoder_out_buffer_ = (T*)allocator_->malloc(sizeof(T) * max_batch_size_ * max_seq_len_ * d_model_, false);

        if (layernorm_type_ == LayerNormType::post_layernorm) {
            normed_from_tensor_ = nullptr;
            normed_attn_out_buf_ = nullptr;
        }
        else {
            normed_from_tensor_ = (T*)allocator_->malloc(sizeof(T) * max_batch_size_ * max_seq_len_ * d_model_, false);
            normed_attn_out_buf_ = (T*)allocator_->malloc(sizeof(T) * max_batch_size_ * max_seq_len_ * d_model_, false);
        }
        is_allocate_buffer_ = true;
    }
}

template<typename T>
void T5Encoder<T>::freeBuffer()
{
    if (is_allocate_buffer_ == true) {
        allocator_->free(token_num_);
        allocator_->free(padding_offset_);
        allocator_->free(trt_mha_padding_offset_);

        allocator_->free(attention_mask_);
        allocator_->free(relative_attention_bias_);
        allocator_->free(t5_encoder_emb_buf_);
        allocator_->free(t5_encoder_in_buffer_);
        allocator_->free(attn_out_buf_);
        allocator_->free(t5_encoder_out_buffer_);

        if (layernorm_type_ == LayerNormType::post_layernorm) {
            normed_from_tensor_ = nullptr;
            normed_attn_out_buf_ = nullptr;
        }
        else {
            allocator_->free(normed_from_tensor_);
            allocator_->free(normed_attn_out_buf_);
        }
        is_allocate_buffer_ = false;
    }
}

template<typename T>
bool T5Encoder<T>::isValidLayerParallelId(uint l)
{
    int local_num_layer = (int)(ceil(num_layer_ * 1.0f / pipeline_para_.world_size_));
    return l < num_layer_ && (l >= local_num_layer * pipeline_para_.rank_)
           && (l < local_num_layer * (pipeline_para_.rank_ + 1));
}

template<typename T>
bool T5Encoder<T>::isFirstLayerParallelId(uint l)
{
    int local_num_layer = (int)(ceil(num_layer_ * 1.0f / pipeline_para_.world_size_));
    return l < num_layer_ && (l == local_num_layer * pipeline_para_.rank_);
}

template<typename T>
bool T5Encoder<T>::isLastLayerParallelId(uint l)
{
    int local_num_layer = (int)(ceil(num_layer_ * 1.0f / pipeline_para_.world_size_));
    return l < num_layer_ && (l == local_num_layer * (pipeline_para_.rank_ + 1) - 1);
}

template<typename T>
int T5Encoder<T>::getFirstLayerParallelId()
{
    int local_num_layer = (int)(ceil(num_layer_ * 1.0f / pipeline_para_.world_size_));
    return local_num_layer * pipeline_para_.rank_;
}

template<typename T>
void T5Encoder<T>::forward(std::vector<Tensor>* output_tensors,
                           const std::vector<Tensor>* input_tensors,
                           const T5EncoderWeight<T>* t5_encoder_weights)
{
    // input_tensors:
    //      input_ids [batch, seqlen]
    //      sequence_length [batch]
    // output tensors:
    //      output hidden state [batch, seqlen, d_model_]

    const size_t request_batch_size = input_tensors->at(0).shape[0];
    const size_t request_seq_len = input_tensors->at(0).shape[1];
    FT_CHECK(input_tensors->size() == 2);
    FT_CHECK(isValidBatchSize(request_batch_size));
    FT_CHECK(isValidSeqLen(request_seq_len));
    FT_CHECK(request_batch_size == input_tensors->at(1).shape[0]);
    FT_CHECK(input_tensors->at(0).shape.size() == 2);
    FT_CHECK(input_tensors->at(1).shape.size() == 1);
    allocateBuffer();

    invokeBuildRelativeAttentionBias(relative_attention_bias_,
                                     t5_encoder_weights->relative_attention_bias,
                                     head_num_,
                                     request_seq_len,
                                     num_bucket_,
                                     true,
                                     max_distance_,
                                     stream_);

    if (attention_type_ == AttentionType::UNFUSED_MHA || attention_type_ == AttentionType::FUSED_MHA) {
        // prevent undefined behavior of the padding parts
        cudaMemset((T*)output_tensors->at(0).data, 0, sizeof(T) * request_batch_size * request_seq_len * d_model_);
    }
    const size_t local_batch_size = getLocalBatchSize(request_batch_size, request_seq_len, pipeline_para_.world_size_);
    const size_t iteration_num = request_batch_size / local_batch_size;
    for (uint ite = 0; ite < iteration_num; ite++) {
        size_t id_offset = ite * local_batch_size;
        size_t d_model_offset = id_offset * request_seq_len * d_model_;

        const int* sequence_lengths = reinterpret_cast<const int*>(input_tensors->at(1).data) + id_offset;

        invokeEmbeddingLookupPosEncoding(t5_encoder_emb_buf_,
                                         t5_encoder_weights->embedding_table,
                                         (T*)nullptr,
                                         (int*)(input_tensors->at(0).data) + id_offset * request_seq_len,
                                         nullptr,
                                         local_batch_size * request_seq_len,
                                         d_model_,
                                         (T)1.0f,
                                         0,
                                         0,
                                         local_batch_size * request_seq_len,
                                         0,
                                         stream_);

        size_t h_token_num;
        T* t5_encoder_input_ptr;
        T* t5_encoder_output_ptr;
        Tensor* padding_offset_tensor_ptr;
        // preprocess (remove padding and build mask)
        switch (attention_type_) {
            case AttentionType::UNFUSED_MHA: {
                invokeBuildEncoderAttentionMask(
                    attention_mask_, sequence_lengths, local_batch_size, request_seq_len, stream_);

                sync_check_cuda_error();
                invokeGetPaddingOffset(&h_token_num,
                                       token_num_,
                                       padding_offset_,
                                       sequence_lengths,
                                       local_batch_size,
                                       request_seq_len,
                                       stream_);
                sync_check_cuda_error();

                if (pipeline_para_.rank_ == 0) {
                    invokeRemovePadding(
                        t5_encoder_in_buffer_, t5_encoder_emb_buf_, padding_offset_, h_token_num, d_model_, stream_);
                    sync_check_cuda_error();
                }
                t5_encoder_input_ptr = t5_encoder_in_buffer_;
                t5_encoder_output_ptr = t5_encoder_out_buffer_;

                padding_offset_tensor_ptr =
                    new Tensor(MEMORY_GPU, TYPE_INT32, std::vector<size_t>{h_token_num}, padding_offset_);
                break;
            }
            case AttentionType::UNFUSED_PADDED_MHA: {
                invokeBuildEncoderAttentionMask(
                    attention_mask_, sequence_lengths, local_batch_size, request_seq_len, stream_);

                sync_check_cuda_error();
                h_token_num = local_batch_size * request_seq_len;
                t5_encoder_input_ptr = (T*)input_tensors->at(0).data + d_model_offset;
                t5_encoder_output_ptr = (T*)output_tensors->at(0).data + d_model_offset;
                padding_offset_tensor_ptr = new Tensor(MEMORY_GPU, TYPE_INT32, std::vector<size_t>{0}, nullptr);
                break;
            }
            case AttentionType::FUSED_MHA: {
                FT_CHECK(false);  // not support FUSED_MHA now
                invokeGetPaddingOffset(&h_token_num,
                                       token_num_,
                                       padding_offset_,
                                       sequence_lengths,
                                       local_batch_size,
                                       request_seq_len,
                                       stream_);

                if (pipeline_para_.rank_ == 0) {
                    invokeRemovePadding(
                        t5_encoder_in_buffer_, t5_encoder_emb_buf_, padding_offset_, h_token_num, d_model_, stream_);
                    sync_check_cuda_error();
                }
                sync_check_cuda_error();
                t5_encoder_input_ptr = t5_encoder_in_buffer_;
                t5_encoder_output_ptr = t5_encoder_out_buffer_;

                invokeGetTrtPaddingOffset(trt_mha_padding_offset_, sequence_lengths, local_batch_size, stream_);

                padding_offset_tensor_ptr = new Tensor(
                    MEMORY_GPU, TYPE_INT32, std::vector<size_t>{local_batch_size + 1}, trt_mha_padding_offset_);
                break;
            }
            case AttentionType::FUSED_PADDED_MHA: {
                FT_CHECK(false);  // not support FUSED_MHA now
                h_token_num = local_batch_size * request_seq_len;
                invokeGetTrtPaddingOffset(
                    trt_mha_padding_offset_, sequence_lengths, local_batch_size, request_seq_len, stream_);
                padding_offset_tensor_ptr = new Tensor(
                    MEMORY_GPU, TYPE_INT32, std::vector<size_t>{local_batch_size * 2 + 1}, trt_mha_padding_offset_);
                t5_encoder_input_ptr = (T*)input_tensors->at(0).data + d_model_offset;
                t5_encoder_output_ptr = (T*)output_tensors->at(0).data + d_model_offset;
                break;
            }
            default: {
                throw std::runtime_error(std::string("[FT][ERROR] Invalid attention type \n"));
            }
        }

        DataType data_type = getTensorType<T>();
        for (uint i = 0; i < num_layer_; i++) {
            if (!isValidLayerParallelId(i)) {
                continue;
            }
            T* from_tensor = (i == 0 ? t5_encoder_input_ptr : t5_encoder_output_ptr);
            T* out_tensor = t5_encoder_output_ptr;

            if (isFirstLayerParallelId(i) && pipeline_para_.rank_ != 0) {
                const int data_size = h_token_num * d_model_ / tensor_para_.world_size_;
                ftNcclRecv(from_tensor + data_size * tensor_para_.rank_,
                           data_size,
                           pipeline_para_.rank_ - 1,
                           pipeline_para_.nccl_comm_,
                           stream_);
                if (tensor_para_.world_size_ > 1) {
                    ftNcclAllGather(
                        from_tensor, from_tensor, data_size, tensor_para_.rank_, tensor_para_.nccl_comm_, stream_);
                }
            }

            if (layernorm_type_ == LayerNormType::pre_layernorm) {
                invokeGeneralT5LayerNorm(normed_from_tensor_,
                                         from_tensor,
                                         t5_encoder_weights->t5_encoder_layer_weights[i]->attn_layernorm_weights.gamma,
                                         h_token_num,
                                         d_model_,
                                         stream_);
            }

            {
                std::vector<Tensor> attn_input_tensors{
                    Tensor{MEMORY_GPU,
                           data_type,
                           std::vector<size_t>{h_token_num, d_model_},
                           layernorm_type_ == LayerNormType::pre_layernorm ? normed_from_tensor_ : from_tensor},
                    Tensor{MEMORY_GPU,
                           data_type,
                           std::vector<size_t>{local_batch_size, 1, request_seq_len, request_seq_len},
                           attention_mask_},
                    *padding_offset_tensor_ptr,
                    Tensor{MEMORY_GPU,
                           data_type,
                           std::vector<size_t>{1, head_num_, request_seq_len, request_seq_len},
                           relative_attention_bias_}};
                std::vector<Tensor> attn_output_tensors{
                    Tensor{MEMORY_GPU, data_type, std::vector<size_t>{h_token_num, d_model_}, attn_out_buf_}};

                attention_layer_->forward(&attn_output_tensors,
                                          &attn_input_tensors,
                                          &t5_encoder_weights->t5_encoder_layer_weights[i]->attention_weights);
            }

            if (layernorm_type_ == LayerNormType::post_layernorm) {
                invokeGeneralAddResidualT5PreLayerNorm(
                    attn_out_buf_,
                    attn_out_buf_,
                    from_tensor,
                    t5_encoder_weights->t5_encoder_layer_weights[i]->attn_layernorm_weights.gamma,
                    h_token_num,
                    d_model_,
                    stream_);
            }
            else if (layernorm_type_ == LayerNormType::pre_layernorm) {
                invokeGeneralAddResidualT5PreLayerNorm(
                    attn_out_buf_,
                    normed_attn_out_buf_,
                    from_tensor,
                    t5_encoder_weights->t5_encoder_layer_weights[i]->ffn_layernorm_weights.gamma,
                    h_token_num,
                    d_model_,
                    stream_);
            }

            // FFN
            {
                std::vector<Tensor> ffn_input_tensors{
                    Tensor{MEMORY_GPU,
                           data_type,
                           std::vector<size_t>{h_token_num, d_model_},
                           layernorm_type_ == LayerNormType::pre_layernorm ? normed_attn_out_buf_ : attn_out_buf_}};
                std::vector<Tensor> ffn_output_tensors{
                    Tensor{MEMORY_GPU, data_type, std::vector<size_t>{h_token_num, d_model_}, out_tensor}};
                ffn_layer_->forward(&ffn_output_tensors,
                                    &ffn_input_tensors,
                                    &t5_encoder_weights->t5_encoder_layer_weights[i]->ffn_weights);
            }

            if (layernorm_type_ == LayerNormType::post_layernorm) {
                invokeGeneralAddResidualT5PreLayerNorm(
                    out_tensor,
                    out_tensor,
                    attn_out_buf_,
                    t5_encoder_weights->t5_encoder_layer_weights[i]->ffn_layernorm_weights.gamma,
                    h_token_num,
                    d_model_,
                    stream_);
            }
            else if (layernorm_type_ == LayerNormType::pre_layernorm) {
                invokeT5AddResidual(out_tensor, attn_out_buf_, h_token_num, d_model_, stream_);
            }
            sync_check_cuda_error();

            if (isLastLayerParallelId(i) == true && pipeline_para_.rank_ != pipeline_para_.world_size_ - 1) {
                const int data_size = h_token_num * d_model_ / tensor_para_.world_size_;
                ftNcclSend(out_tensor + data_size * tensor_para_.rank_,
                           data_size,
                           pipeline_para_.rank_ + 1,
                           pipeline_para_.nccl_comm_,
                           stream_);
            }
        }

        if (pipeline_para_.rank_ == pipeline_para_.world_size_ - 1) {
            if (layernorm_type_ == LayerNormType::pre_layernorm) {
                invokeGeneralT5LayerNorm(t5_encoder_output_ptr,
                                         t5_encoder_output_ptr,
                                         t5_encoder_weights->post_transformer_layernorm_weights.gamma,
                                         h_token_num,
                                         d_model_,
                                         stream_);
            }

            // post process (rebuild padding)
            switch (attention_type_) {
                case AttentionType::UNFUSED_MHA: {
                    if (pipeline_para_.rank_ == pipeline_para_.world_size_ - 1) {
                        invokeRebuildPadding((T*)output_tensors->at(0).data + d_model_offset,
                                             t5_encoder_out_buffer_,
                                             padding_offset_,
                                             h_token_num,
                                             d_model_,
                                             stream_);
                    }
                    break;
                }
                case AttentionType::UNFUSED_PADDED_MHA: {
                    break;
                }
                case AttentionType::FUSED_MHA: {
                    if (pipeline_para_.rank_ == pipeline_para_.world_size_ - 1) {
                        invokeRebuildPadding((T*)output_tensors->at(0).data + d_model_offset,
                                             t5_encoder_out_buffer_,
                                             padding_offset_,
                                             h_token_num,
                                             d_model_,
                                             stream_);
                    }
                    break;
                }
                case AttentionType::FUSED_PADDED_MHA: {
                    break;
                }
                default: {
                    throw std::runtime_error(std::string("[FT][ERROR] Invalid attention type \n"));
                }
            }
        }

        if (is_free_buffer_after_forward_ == true) {
            freeBuffer();
        }
        sync_check_cuda_error();

        delete padding_offset_tensor_ptr;
    }

    if (pipeline_para_.world_size_ > 1) {
        NCCLCHECK(ncclGroupStart());
        const int data_size = request_batch_size * request_seq_len * d_model_ / tensor_para_.world_size_;
        ftNcclBroadCast((T*)output_tensors->at(0).data + data_size * tensor_para_.rank_,
                        data_size,
                        pipeline_para_.world_size_ - 1,
                        pipeline_para_.nccl_comm_,
                        stream_);

        NCCLCHECK(ncclGroupEnd());
        check_cuda_error(cudaStreamSynchronize(stream_));
        sync_check_cuda_error();
        if (tensor_para_.world_size_ > 1) {
            ftNcclAllGather((T*)output_tensors->at(0).data,
                            (T*)output_tensors->at(0).data,
                            data_size,
                            tensor_para_.rank_,
                            tensor_para_.nccl_comm_,
                            stream_);
        }
    }
}

template<typename T>
bool T5Encoder<T>::isValidBatchSize(size_t batch_size)
{
    if (batch_size <= max_batch_size_) {
        return true;
    }
    else {
        freeBuffer();
        max_batch_size_ = batch_size * 1.2;
        return true;
    }
}

template<typename T>
bool T5Encoder<T>::isValidSeqLen(size_t seq_len)
{
    if (seq_len <= max_seq_len_) {
        return true;
    }
    else {
        freeBuffer();
        max_seq_len_ = seq_len * 1.2;
        return true;
    }
}

template class T5Encoder<float>;
template class T5Encoder<half>;

}  // namespace fastertransformer
