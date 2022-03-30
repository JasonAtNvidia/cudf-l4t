/*
 * Copyright (c) 2020-2022, NVIDIA CORPORATION.
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

#include <cuda_runtime.h>
#include <cudf/column/column_device_view.cuh>
#include <cudf/lists/lists_column_view.hpp>
#include <cudf/types.hpp>

namespace cudf {

namespace detail {

/**
 * @brief Given a column_device_view, an instance of this class provides a
 * wrapper on this compound column for list operations.
 * Analogous to list_column_view.
 */
class lists_column_device_view {
 public:
  lists_column_device_view()                                = delete;
  ~lists_column_device_view()                               = default;
  lists_column_device_view(lists_column_device_view const&) = default;
  lists_column_device_view(lists_column_device_view&&)      = default;
  lists_column_device_view& operator=(lists_column_device_view const&) = default;
  lists_column_device_view& operator=(lists_column_device_view&&) = default;

  CUDF_HOST_DEVICE lists_column_device_view(column_device_view const& underlying_)
    : underlying(underlying_)
  {
#ifdef __CUDA_ARCH__
    cudf_assert(underlying.type().id() == type_id::LIST and
                "lists_column_device_view only supports lists");
#else
    CUDF_EXPECTS(underlying_.type().id() == type_id::LIST,
                 "lists_column_device_view only supports lists");
#endif
  }

  /**
   * @brief Fetches number of rows in the lists column
   */
  [[nodiscard]] CUDF_HOST_DEVICE inline cudf::size_type size() const { return underlying.size(); }

  /**
   * @brief Fetches the offsets column of the underlying list column.
   */
  [[nodiscard]] __device__ inline column_device_view offsets() const
  {
    return underlying.child(lists_column_view::offsets_column_index);
  }

  /**
   * @brief Fetches the list offset value at a given row index while taking column offset into
   * account.
   */
  [[nodiscard]] __device__ inline size_type offset_at(size_type idx) const
  {
    return underlying.child(lists_column_view::offsets_column_index)
      .element<size_type>(offset() + idx);
  }

  /**
   * @brief Fetches the child column of the underlying list column.
   */
  [[nodiscard]] __device__ inline column_device_view child() const
  {
    return underlying.child(lists_column_view::child_column_index);
  }

  /**
   * @brief Fetches the child column of the underlying list column with offset and size applied
   */
  [[nodiscard]] __device__ inline column_device_view sliced_child() const
  {
    auto start = offset_at(0);
    auto end   = offset_at(size());
    return child().slice(start, end - start);
  }

  /**
   * @brief Indicates whether the list column is nullable.
   */
  [[nodiscard]] CUDF_HOST_DEVICE inline bool nullable() const { return underlying.nullable(); }

  /**
   * @brief Indicates whether the row (i.e. list) at the specified
   * index is null.
   */
  [[nodiscard]] __device__ inline bool is_null(size_type idx) const
  {
    return underlying.is_null(idx);
  }

  /**
   * @brief Fetches the offset of the underlying column_device_view,
   *        in case it is a sliced/offset column.
   */
  [[nodiscard]] CUDF_HOST_DEVICE inline size_type offset() const { return underlying.offset(); }

 private:
  column_device_view underlying;
};

}  // namespace detail

}  // namespace cudf
