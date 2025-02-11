/*
 * Copyright (c) 2019-2022, NVIDIA CORPORATION.
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

#include <cudf/types.hpp>
#include <cudf/utilities/default_stream.hpp>

#include <rmm/cuda_stream_view.hpp>

#include <memory>

namespace cudf {
namespace detail {

/**
 * @copydoc cudf::repeat(table_view const&, column_view const&, bool,
 * rmm::mr::device_memory_resource*)
 *
 * @param stream CUDA stream used for device memory operations and kernel launches.
 */
std::unique_ptr<table> repeat(
  table_view const& input_table,
  column_view const& count,
  bool check_count,
  rmm::cuda_stream_view stream        = cudf::default_stream_value,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_current_device_resource());

/**
 * @copydoc cudf::repeat(table_view const&, size_type,
 * rmm::mr::device_memory_resource*)
 *
 * @param stream CUDA stream used for device memory operations and kernel launches.
 */
std::unique_ptr<table> repeat(
  table_view const& input_table,
  size_type count,
  rmm::cuda_stream_view stream        = cudf::default_stream_value,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_current_device_resource());

}  // namespace detail
}  // namespace cudf
