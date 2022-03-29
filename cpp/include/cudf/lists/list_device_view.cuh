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
#include <cudf/lists/lists_column_device_view.cuh>
#include <cudf/types.hpp>
#include <cudf/utilities/type_dispatcher.hpp>

namespace cudf {

/**
 * @brief A non-owning, immutable view of device data that represents
 * a list of elements of arbitrary type (including further nested lists).
 */
class list_device_view {
  using lists_column_device_view = cudf::detail::lists_column_device_view;

 public:
  list_device_view() = default;

  __device__ inline list_device_view(lists_column_device_view const& lists_column,
                                     size_type const& row_index)
    : lists_column(lists_column), _row_index(row_index)
  {
    column_device_view const& offsets = lists_column.offsets();
    cudf_assert(row_index >= 0 && row_index < lists_column.size() && row_index < offsets.size() &&
                "row_index out of bounds");

    begin_offset = offsets.element<size_type>(row_index + lists_column.offset());
    cudf_assert(begin_offset >= 0 && begin_offset <= lists_column.child().size() &&
                "begin_offset out of bounds.");
    _size = offsets.element<size_type>(row_index + 1 + lists_column.offset()) - begin_offset;
  }

  ~list_device_view() = default;

  /**
   * @brief Fetches the offset in the list column's child that corresponds to
   * the element at the specified list index.
   *
   * Consider the following lists column:
   *  [
   *   [0,1,2],
   *   [3,4,5],
   *   [6,7,8]
   *  ]
   *
   * The list's internals would look like:
   *  offsets: [0, 3, 6, 9]
   *  child  : [0, 1, 2, 3, 4, 5, 6, 7, 8]
   *
   * The second list row (i.e. row_index=1) is [3,4,5].
   * The third element (i.e. idx=2) of the second list row is 5.
   *
   * The offset of this element as stored in the child column (i.e. 5)
   * may be fetched using this method.
   */
  [[nodiscard]] __device__ inline size_type element_offset(size_type idx) const
  {
    cudf_assert(idx >= 0 && idx < size() && "idx out of bounds");
    return begin_offset + idx;
  }

  /**
   * @brief Fetches the element at the specified index within the list row.
   *
   * @tparam T The type of the list's element.
   * @param idx The index into the list row
   * @return The element at the specified index of the list row.
   */
  template <typename T>
  __device__ inline T element(size_type idx) const
  {
    return lists_column.child().element<T>(element_offset(idx));
  }

  /**
   * @brief Checks whether element is null at specified index in the list row.
   */
  [[nodiscard]] __device__ inline bool is_null(size_type idx) const
  {
    cudf_assert(idx >= 0 && idx < size() && "Index out of bounds.");
    auto element_offset = begin_offset + idx;
    return lists_column.child().is_null(element_offset);
  }

  /**
   * @brief Checks whether this list row is null.
   */
  [[nodiscard]] __device__ inline bool is_null() const { return lists_column.is_null(_row_index); }

  /**
   * @brief Fetches the number of elements in this list row.
   */
  [[nodiscard]] __device__ inline size_type size() const { return _size; }

  /**
   * @brief Fetches the lists_column_device_view that contains this list.
   */
  [[nodiscard]] __device__ inline lists_column_device_view const& get_column() const
  {
    return lists_column;
  }

  template <typename T>
  struct pair_accessor;

  template <typename T>
  struct pair_rep_accessor;

  template <typename T>
  using const_pair_iterator =
    thrust::transform_iterator<pair_accessor<T>, thrust::counting_iterator<cudf::size_type>>;

  template <typename T>
  using const_pair_rep_iterator =
    thrust::transform_iterator<pair_rep_accessor<T>, thrust::counting_iterator<cudf::size_type>>;

  /**
   * @brief Fetcher for a pair iterator to the first element in the list_device_view.
   *
   * Dereferencing the returned iterator yields a `thrust::pair<T, bool>`.
   *
   * If the element at index `i` is valid, then for `p = iter[i]`,
   *   1. `p.first` is the value of the element at `i`
   *   2. `p.second == true`
   *
   * If the element at index `i` is null,
   *   1. `p.first` is undefined
   *   2. `p.second == false`
   */
  template <typename T>
  [[nodiscard]] __device__ inline const_pair_iterator<T> pair_begin() const
  {
    return const_pair_iterator<T>{thrust::counting_iterator<size_type>(0), pair_accessor<T>{*this}};
  }

  /**
   * @brief Fetcher for a pair iterator to one position past the last element in the
   * list_device_view.
   */
  template <typename T>
  [[nodiscard]] __device__ inline const_pair_iterator<T> pair_end() const
  {
    return const_pair_iterator<T>{thrust::counting_iterator<size_type>(size()),
                                  pair_accessor<T>{*this}};
  }

  /**
   * @brief Fetcher for a pair iterator to the first element in the list_device_view.
   *
   * Dereferencing the returned iterator yields a `thrust::pair<rep_type, bool>`,
   * where `rep_type` is `device_storage_type_t<T>`, the type used to store the value
   * on the device.
   *
   * If the element at index `i` is valid, then for `p = iter[i]`,
   *   1. `p.first` is the value of the element at `i`
   *   2. `p.second == true`
   *
   * If the element at index `i` is null,
   *   1. `p.first` is undefined
   *   2. `p.second == false`
   */
  template <typename T>
  [[nodiscard]] __device__ inline const_pair_rep_iterator<T> pair_rep_begin() const
  {
    return const_pair_rep_iterator<T>{thrust::counting_iterator<size_type>(0),
                                      pair_rep_accessor<T>{*this}};
  }

  /**
   * @brief Fetcher for a pair iterator to one position past the last element in the
   * list_device_view.
   */
  template <typename T>
  [[nodiscard]] __device__ inline const_pair_rep_iterator<T> pair_rep_end() const
  {
    return const_pair_rep_iterator<T>{thrust::counting_iterator<size_type>(size()),
                                      pair_rep_accessor<T>{*this}};
  }

 private:
  lists_column_device_view const& lists_column;
  size_type _row_index{};  // Row index in the Lists column vector.
  size_type _size{};       // Number of elements in *this* list row.

  size_type begin_offset;  // Offset in list_column_device_view where this list begins.

  /**
   * @brief pair accessor for elements in a `list_device_view`
   *
   * This unary functor returns a pair of:
   *   1. data element at a specified index
   *   2. boolean validity flag for that element
   *
   * @tparam T The element-type of the list row
   */
  template <typename T>
  struct pair_accessor {
    list_device_view const& list;

    /**
     * @brief constructor
     *
     * @param _list The `list_device_view` whose rows are being accessed.
     */
    explicit CUDF_HOST_DEVICE inline pair_accessor(list_device_view const& _list) : list{_list} {}

    /**
     * @brief Accessor for the {data, validity} pair at the specified index
     *
     * @param i Index into the list_device_view
     * @return A pair of data element and its validity flag.
     */
    __device__ inline thrust::pair<T, bool> operator()(cudf::size_type i) const
    {
      return {list.element<T>(i), !list.is_null(i)};
    }
  };

  /**
   * @brief pair rep accessor for elements in a `list_device_view`
   *
   * Returns a `pair<rep_type, bool>`, where `rep_type` = `device_storage_type_t<T>`,
   * the type used to store the value on the device.
   *
   * This unary functor returns a pair of:
   *   1. rep element at a specified index
   *   2. boolean validity flag for that element
   *
   * @tparam T The element-type of the list row
   */
  template <typename T>
  struct pair_rep_accessor {
    list_device_view const& list;

    using rep_type = device_storage_type_t<T>;

    /**
     * @brief constructor
     *
     * @param _list The `list_device_view` whose rows are being accessed.
     */
    explicit CUDF_HOST_DEVICE inline pair_rep_accessor(list_device_view const& _list) : list{_list}
    {
    }

    /**
     * @brief Accessor for the {rep_data, validity} pair at the specified index
     *
     * @param i Index into the list_device_view
     * @return A pair of data element and its validity flag.
     */
    __device__ inline thrust::pair<rep_type, bool> operator()(cudf::size_type i) const
    {
      return {get_rep<T>(i), !list.is_null(i)};
    }

   private:
    template <typename R, std::enable_if_t<std::is_same_v<R, rep_type>, void>* = nullptr>
    __device__ inline rep_type get_rep(cudf::size_type i) const
    {
      return list.element<R>(i);
    }

    template <typename R, std::enable_if_t<not std::is_same_v<R, rep_type>, void>* = nullptr>
    __device__ inline rep_type get_rep(cudf::size_type i) const
    {
      return list.element<R>(i).value();
    }
  };
};

/**
 * @brief returns size of the list by row index
 *
 */
struct list_size_functor {
  detail::lists_column_device_view const d_column;
#pragma nv_exec_check_disable
  CUDF_HOST_DEVICE inline list_size_functor(detail::lists_column_device_view const& d_col)
    : d_column(d_col)
  {
  }
  __device__ inline size_type operator()(size_type idx)
  {
    if (d_column.is_null(idx)) return size_type{0};
    return d_column.offset_at(idx + 1) - d_column.offset_at(idx);
  }
};

__device__ auto inline make_list_size_iterator(detail::lists_column_device_view const& c)
{
  return thrust::make_transform_iterator(thrust::make_counting_iterator(cudf::size_type{0}),
                                         list_size_functor{c});
}

}  // namespace cudf
