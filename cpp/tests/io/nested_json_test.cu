/*
 * Copyright (c) 2022, NVIDIA CORPORATION.
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

#include <io/json/nested_json.hpp>
#include <io/utilities/hostdevice_vector.hpp>

#include <cudf_test/base_fixture.hpp>
#include <cudf_test/cudf_gtest.hpp>

#include <rmm/cuda_stream.hpp>
#include <rmm/cuda_stream_view.hpp>

namespace cuio_json = cudf::io::json;

// Base test fixture for tests
struct JsonTest : public cudf::test::BaseFixture {
};

TEST_F(JsonTest, StackContext)
{
  // Type used to represent the atomic symbol type used within the finite-state machine
  using SymbolT      = char;
  using StackSymbolT = char;

  // Prepare cuda stream for data transfers & kernels
  rmm::cuda_stream stream{};
  rmm::cuda_stream_view stream_view(stream);

  // Test input
  std::string input = R"(  [{)"
                      R"("category": "reference",)"
                      R"("index:": [4,12,42],)"
                      R"("author": "Nigel Rees",)"
                      R"("title": "[Sayings of the Century]",)"
                      R"("price": 8.95)"
                      R"(},  )"
                      R"({)"
                      R"("category": "reference",)"
                      R"("index": [4,{},null,{"a":[{ }, {}] } ],)"
                      R"("author": "Nigel Rees",)"
                      R"("title": "{}\\\"[], <=semantic-symbols-string\\\\",)"
                      R"("price": 8.95)"
                      R"(}] )";

  // Prepare input & output buffers
  rmm::device_uvector<SymbolT> d_input(input.size(), stream_view);
  hostdevice_vector<StackSymbolT> stack_context(input.size(), stream_view);

  ASSERT_CUDA_SUCCEEDED(cudaMemcpyAsync(d_input.data(),
                                        input.data(),
                                        input.size() * sizeof(SymbolT),
                                        cudaMemcpyHostToDevice,
                                        stream.value()));

  // Run algorithm
  cuio_json::detail::get_stack_context(d_input, stack_context.device_ptr(), stream_view);

  // Copy back the results
  stack_context.device_to_host(stream_view);

  // Make sure we copied back the stack context
  stream_view.synchronize();

  std::vector<char> golden_stack_context{
    '_', '_', '_', '[', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '[', '[', '[', '[', '[', '[', '[', '[', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '[', '[', '[', '[', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '[', '[', '[', '{', '[', '[', '[', '[', '[', '[', '[', '{',
    '{', '{', '{', '{', '[', '{', '{', '[', '[', '[', '{', '[', '{', '{', '[', '[', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '[', '_'};

  ASSERT_EQ(golden_stack_context.size(), stack_context.size());
  CUDF_TEST_EXPECT_VECTOR_EQUAL(golden_stack_context, stack_context, stack_context.size());
}

TEST_F(JsonTest, StackContextUtf8)
{
  // Type used to represent the atomic symbol type used within the finite-state machine
  using SymbolT      = char;
  using StackSymbolT = char;

  // Prepare cuda stream for data transfers & kernels
  rmm::cuda_stream stream{};
  rmm::cuda_stream_view stream_view(stream);

  // Test input
  std::string input = R"([{"a":{"year":1882,"author": "Bharathi"}, {"a":"filip ʒakotɛ"}}])";

  // Prepare input & output buffers
  rmm::device_uvector<SymbolT> d_input(input.size(), stream_view);
  hostdevice_vector<StackSymbolT> stack_context(input.size(), stream_view);

  ASSERT_CUDA_SUCCEEDED(cudaMemcpyAsync(d_input.data(),
                                        input.data(),
                                        input.size() * sizeof(SymbolT),
                                        cudaMemcpyHostToDevice,
                                        stream.value()));

  // Run algorithm
  cuio_json::detail::get_stack_context(d_input, stack_context.device_ptr(), stream_view);

  // Copy back the results
  stack_context.device_to_host(stream_view);

  // Make sure we copied back the stack context
  stream_view.synchronize();

  std::vector<char> golden_stack_context{
    '_', '[', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{',
    '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '{', '['};

  ASSERT_EQ(golden_stack_context.size(), stack_context.size());
  CUDF_TEST_EXPECT_VECTOR_EQUAL(golden_stack_context, stack_context, stack_context.size());
}

TEST_F(JsonTest, TokenStream)
{
  using cuio_json::PdaTokenT;
  using cuio_json::SymbolOffsetT;
  using cuio_json::SymbolT;

  constexpr std::size_t single_item = 1;

  // Prepare cuda stream for data transfers & kernels
  rmm::cuda_stream stream{};
  rmm::cuda_stream_view stream_view(stream);

  // Test input
  std::string input = R"(  [{)"
                      R"("category": "reference",)"
                      R"("index:": [4,12,42],)"
                      R"("author": "Nigel Rees",)"
                      R"("title": "[Sayings of the Century]",)"
                      R"("price": 8.95)"
                      R"(},  )"
                      R"({)"
                      R"("category": "reference",)"
                      R"("index": [4,{},null,{"a":[{ }, {}] } ],)"
                      R"("author": "Nigel Rees",)"
                      R"("title": "{}[], <=semantic-symbols-string",)"
                      R"("price": 8.95)"
                      R"(}] )";

  // Prepare input & output buffers
  rmm::device_uvector<SymbolT> d_input(input.size(), stream_view);

  ASSERT_CUDA_SUCCEEDED(cudaMemcpyAsync(d_input.data(),
                                        input.data(),
                                        input.size() * sizeof(SymbolT),
                                        cudaMemcpyHostToDevice,
                                        stream.value()));

  hostdevice_vector<PdaTokenT> tokens_gpu{input.size(), stream_view};
  hostdevice_vector<SymbolOffsetT> token_indices_gpu{input.size(), stream_view};
  hostdevice_vector<SymbolOffsetT> num_tokens_out{single_item, stream_view};

  // Parse the JSON and get the token stream
  cuio_json::detail::get_token_stream(d_input,
                                      tokens_gpu.device_ptr(),
                                      token_indices_gpu.device_ptr(),
                                      num_tokens_out.device_ptr(),
                                      stream_view);

  // Copy back the number of tokens that were written
  num_tokens_out.device_to_host(stream_view);
  tokens_gpu.device_to_host(stream_view);
  token_indices_gpu.device_to_host(stream_view);

  // Make sure we copied back all relevant data
  stream_view.synchronize();

  // Golden token stream sample
  using token_t = cuio_json::token_t;
  std::vector<std::pair<std::size_t, cuio_json::PdaTokenT>> golden_token_stream = {
    {2, token_t::ListBegin},        {3, token_t::StructBegin},      {4, token_t::FieldNameBegin},
    {13, token_t::FieldNameEnd},    {16, token_t::StringBegin},     {26, token_t::StringEnd},
    {28, token_t::FieldNameBegin},  {35, token_t::FieldNameEnd},    {38, token_t::ListBegin},
    {39, token_t::ValueBegin},      {40, token_t::ValueEnd},        {41, token_t::ValueBegin},
    {43, token_t::ValueEnd},        {44, token_t::ValueBegin},      {46, token_t::ValueEnd},
    {46, token_t::ListEnd},         {48, token_t::FieldNameBegin},  {55, token_t::FieldNameEnd},
    {58, token_t::StringBegin},     {69, token_t::StringEnd},       {71, token_t::FieldNameBegin},
    {77, token_t::FieldNameEnd},    {80, token_t::StringBegin},     {105, token_t::StringEnd},
    {107, token_t::FieldNameBegin}, {113, token_t::FieldNameEnd},   {116, token_t::ValueBegin},
    {120, token_t::ValueEnd},       {120, token_t::StructEnd},      {124, token_t::StructBegin},
    {125, token_t::FieldNameBegin}, {134, token_t::FieldNameEnd},   {137, token_t::StringBegin},
    {147, token_t::StringEnd},      {149, token_t::FieldNameBegin}, {155, token_t::FieldNameEnd},
    {158, token_t::ListBegin},      {159, token_t::ValueBegin},     {160, token_t::ValueEnd},
    {161, token_t::StructBegin},    {162, token_t::StructEnd},      {164, token_t::ValueBegin},
    {168, token_t::ValueEnd},       {169, token_t::StructBegin},    {170, token_t::FieldNameBegin},
    {172, token_t::FieldNameEnd},   {174, token_t::ListBegin},      {175, token_t::StructBegin},
    {177, token_t::StructEnd},      {180, token_t::StructBegin},    {181, token_t::StructEnd},
    {182, token_t::ListEnd},        {184, token_t::StructEnd},      {186, token_t::ListEnd},
    {188, token_t::FieldNameBegin}, {195, token_t::FieldNameEnd},   {198, token_t::StringBegin},
    {209, token_t::StringEnd},      {211, token_t::FieldNameBegin}, {217, token_t::FieldNameEnd},
    {220, token_t::StringBegin},    {252, token_t::StringEnd},      {254, token_t::FieldNameBegin},
    {260, token_t::FieldNameEnd},   {263, token_t::ValueBegin},     {267, token_t::ValueEnd},
    {267, token_t::StructEnd},      {268, token_t::ListEnd}};

  // Verify the number of tokens matches
  ASSERT_EQ(golden_token_stream.size(), num_tokens_out[0]);

  for (std::size_t i = 0; i < num_tokens_out[0]; i++) {
    // Ensure the index the tokens are pointing to do match
    EXPECT_EQ(golden_token_stream[i].first, token_indices_gpu[i]) << "Mismatch at #" << i;

    // Ensure the token category is correct
    EXPECT_EQ(golden_token_stream[i].second, tokens_gpu[i]) << "Mismatch at #" << i;
  }
}
