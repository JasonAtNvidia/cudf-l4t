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

#include <stream_compaction/stream_compaction_common.cuh>
#include <stream_compaction/stream_compaction_common.hpp>

#include <cudf/column/column_factories.hpp>
#include <cudf/detail/iterator.cuh>
#include <cudf/table/experimental/row_operators.cuh>
#include <cudf/table/table_view.hpp>

#include <rmm/cuda_stream_view.hpp>
#include <rmm/exec_policy.hpp>

#include <thrust/find.h>
#include <thrust/iterator/counting_iterator.h>
#include <thrust/uninitialized_fill.h>

namespace cudf::detail {

/**
 * @brief Check if the (unique) row of the `needle` column is contained in the `haystack` column.
 *
 * If the input `needle` column has more than one row, only the first row will be considered.
 *
 * This function is designed for nested types. It can also work with non-nested types
 * but with lower performance due to the complexity of the implementation.
 */
bool contains_nested_element(column_view const& haystack,
                             column_view const& needle,
                             rmm::cuda_stream_view stream)
{
  CUDF_EXPECTS(needle.size() > 0, "Input needle column should have ONE row.");

  auto const haystack_tv = table_view{{haystack}};
  auto const needle_tv   = table_view{{needle}};
  auto const has_nulls   = has_nested_nulls(haystack_tv) || has_nested_nulls(needle_tv);

  auto const comparator =
    cudf::experimental::row::equality::two_table_comparator(haystack_tv, needle_tv, stream);
  auto const d_comp = comparator.device_comparator(nullate::DYNAMIC{has_nulls});

  auto const begin = cudf::experimental::row::lhs_iterator(0);
  auto const end   = begin + haystack.size();
  using cudf::experimental::row::rhs_index_type;

  auto const found_it = [&] {
    if (haystack.has_nulls()) {
      auto const haystack_cdv_ptr  = column_device_view::create(haystack, stream);
      auto const haystack_valid_it = cudf::detail::make_validity_iterator<false>(*haystack_cdv_ptr);
      return thrust::find_if(
        rmm::exec_policy(stream),
        begin,
        end,
        [d_comp, haystack_valid_it] __device__(auto const idx) {
          if (!haystack_valid_it[static_cast<size_type>(idx)]) { return false; }
          return d_comp(idx,
                        static_cast<rhs_index_type>(0));  // compare haystack[idx] == needle[0].
        });

    } else {
      return thrust::find_if(
        rmm::exec_policy(stream), begin, end, [d_comp] __device__(auto const idx) {
          return d_comp(idx,
                        static_cast<rhs_index_type>(0));  // compare haystack[idx] == needle[0].
        });
    }
  }();

  return found_it != end;
}

/**
 * @brief The adapter struct for table comparator with strong index types
 *
 * This adapter utility converts the positive integer indices into `lhs_index_type` and negative
 * indices into `rhs_index_type`. Before such conversion happening, it also shifts the negative
 * indices (which are given in the range [-1, -size - 1) into the correct range [0, size).
 */
template <typename Comparator>
struct table_comparator_adapter {
  table_comparator_adapter(Comparator&& comp_) : comp(std::move(comp_)) {}

  __device__ bool operator()(size_type const i, size_type const j) const noexcept
  {
    using cudf::experimental::row::lhs_index_type;
    using cudf::experimental::row::rhs_index_type;

    auto const lhs_idx = static_cast<lhs_index_type>(i >= 0 ? i : j);
    auto const rhs_idx = static_cast<rhs_index_type>(i < 0 ? -(i + 1) : -(j + 1));
    return comp(lhs_idx, rhs_idx);
  }
  Comparator const comp;
};

/**
 * @brief Check if each row of the `needles` column is contained in the `haystack` column,
 * specialized for nested type.
 *
 * This function is designed for nested types. It can also work with non-nested types
 * but with lower performance due to the complexity of the implementation.
 *
 */
std::unique_ptr<column> multi_contains_nested_elements(column_view const& haystack,
                                                       column_view const& needles,
                                                       rmm::cuda_stream_view stream,
                                                       rmm::mr::device_memory_resource* mr)
{
  auto result = make_numeric_column(data_type{type_to_id<bool>()},
                                    needles.size(),
                                    copy_bitmask(needles),
                                    needles.null_count(),
                                    stream,
                                    mr);
  if (needles.is_empty()) { return result; }
  (void)haystack;

  auto const out_begin = result->mutable_view().template begin<bool>();
  if (haystack.is_empty()) {
    thrust::uninitialized_fill(
      rmm::exec_policy(stream), out_begin, out_begin + needles.size(), false);
    return result;
  }

  auto const haystack_tv        = table_view{{haystack}};
  auto const needles_tv         = table_view{{needles}};
  auto const haystack_has_nulls = has_nested_nulls(haystack_tv);
  auto const needles_has_nulls  = has_nested_nulls(needles_tv);

  auto haystack_map =
    detail::hash_map_type{compute_hash_table_size(haystack.size()),
                          detail::COMPACTION_EMPTY_KEY_SENTINEL,
                          detail::COMPACTION_EMPTY_VALUE_SENTINEL,
                          detail::hash_table_allocator_type{default_allocator<char>{}, stream},
                          stream.value()};

  // Insert all indices of the elements in the haystack column into the hash map.
  // As such, we will use `thrust::equal_to` as key comparator.
  {
    auto const haystack_it = cudf::detail::make_counting_transform_iterator(
      0, [] __device__(size_type const i) { return cuco::make_pair(i, i); });

    auto const hasher   = cudf::experimental::row::hash::row_hasher(haystack_tv, stream);
    auto const d_hasher = detail::experimental::compaction_hash(
      hasher.device_hasher(nullate::DYNAMIC{haystack_has_nulls}));

    haystack_map.insert(haystack_it,
                        haystack_it + haystack.size(),
                        d_hasher,
                        thrust::equal_to<size_type>{},
                        stream.value());
  }

  // Check for existence of needles in haystack.
  // During this, we will use `table_comparator_adapter` to recognize and convert the existing
  // indices in the hash map into `lhs_index_type`, and indices of the searching needles into
  // `rhs_index_type` for table comparison.
  {
    // Use negative indices so the comparator can recognize and convert them to `rhs_index_type`.
    // Note that needle indices will be supplied in the range [-1, -1 - neeedles.size()), thus they
    // also need to be converted back to the range [0, needles.size()).
    auto const search_it = thrust::make_reverse_iterator(thrust::make_counting_iterator(-1));

    auto const hasher   = cudf::experimental::row::hash::row_hasher(needles_tv, stream);
    auto const d_hasher = detail::experimental::compaction_hash(
      hasher.device_hasher(nullate::DYNAMIC{needles_has_nulls}));

    auto const comparator =
      cudf::experimental::row::equality::two_table_comparator(haystack_tv, needles_tv, stream);

    haystack_map.contains(search_it,
                          search_it + needles.size(),
                          out_begin,
                          d_hasher,
                          table_comparator_adapter{comparator.device_comparator(
                            nullate::DYNAMIC{haystack_has_nulls || needles_has_nulls})},
                          stream.value());
  }

  return result;
}

}  // namespace cudf::detail
