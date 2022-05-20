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

#include <hash/unordered_multiset.cuh>
#include <search/utilities.hpp>
#include <stream_compaction/stream_compaction_common.cuh>
#include <stream_compaction/stream_compaction_common.hpp>

#include <cudf/column/column_factories.hpp>
#include <cudf/detail/iterator.cuh>
#include <cudf/detail/nvtx/ranges.hpp>
#include <cudf/detail/search.hpp>
#include <cudf/dictionary/detail/search.hpp>
#include <cudf/dictionary/detail/update_keys.hpp>
#include <cudf/lists/list_view.hpp>
#include <cudf/lists/lists_column_view.hpp>
#include <cudf/scalar/scalar.hpp>
#include <cudf/structs/struct_view.hpp>
#include <cudf/table/row_operators.cuh>
#include <cudf/table/table_device_view.cuh>
#include <cudf/table/table_view.hpp>

#include <rmm/cuda_stream_view.hpp>
#include <rmm/device_uvector.hpp>
#include <rmm/exec_policy.hpp>

#include <thrust/fill.h>
#include <thrust/find.h>
#include <thrust/iterator/counting_iterator.h>
#include <thrust/pair.h>
#include <thrust/transform.h>
#include <thrust/uninitialized_fill.h>

namespace cudf {
namespace detail {

namespace {

struct contains_scalar_dispatch {
  template <typename Type>
  std::enable_if_t<!is_nested<Type>(), bool> operator()(column_view const& haystack,
                                                        scalar const& needle,
                                                        rmm::cuda_stream_view stream) const
  {
    CUDF_EXPECTS(haystack.type() == needle.type(), "scalar and column types must match");

    using DType           = device_storage_type_t<Type>;
    using ScalarType      = cudf::scalar_type_t<Type>;
    auto const d_haystack = column_device_view::create(haystack, stream);
    auto const s          = static_cast<ScalarType const*>(&needle);

    auto const check_contain = [stream](auto const& begin, auto const& end, auto const& val) {
      auto const found_it = thrust::find(rmm::exec_policy(stream), begin, end, val);
      return found_it != end;
    };

    if (haystack.has_nulls()) {
      auto const begin = d_haystack->pair_begin<DType, true>();
      auto const end   = d_haystack->pair_end<DType, true>();
      auto const val   = thrust::make_pair(s->value(stream), true);

      return check_contain(begin, end, val);
    } else {
      auto const begin = d_haystack->begin<DType>();
      auto const end   = d_haystack->end<DType>();
      auto const val   = s->value(stream);

      return check_contain(begin, end, val);
    }
  }

  template <typename Type>
  std::enable_if_t<is_nested<Type>(), bool> operator()(column_view const& haystack,
                                                       scalar const& needle,
                                                       rmm::cuda_stream_view stream) const
  {
    // Haystack and needle structure compatibility will be checked by the table comparator
    // constructor during calling to `contains_nested_element`.
    CUDF_EXPECTS(haystack.type() == needle.type(), "scalar and column types must match");

    auto const [needle_cv, _] = nested_type_scalar_to_column_view(needle, stream);
    return contains_nested_element(haystack, needle_cv, stream);
  }
};

template <>
bool contains_scalar_dispatch::operator()<cudf::dictionary32>(column_view const& haystack,
                                                              scalar const& needle,
                                                              rmm::cuda_stream_view stream) const
{
  auto const dict_col = cudf::dictionary_column_view(haystack);
  // first, find the needle in the dictionary's key set
  auto const index = cudf::dictionary::detail::get_index(dict_col, needle, stream);
  // if found, check the index is actually in the indices column
  return index->is_valid(stream) && cudf::type_dispatcher(dict_col.indices().type(),
                                                          contains_scalar_dispatch{},
                                                          dict_col.indices(),
                                                          *index,
                                                          stream);
}

struct multi_contains_dispatch {
  template <typename Type>
  std::enable_if_t<!is_nested<Type>(), std::unique_ptr<column>> operator()(
    column_view const& haystack,
    column_view const& needles,
    rmm::cuda_stream_view stream,
    rmm::mr::device_memory_resource* mr) const
  {
    auto result = make_numeric_column(data_type{type_to_id<bool>()},
                                      needles.size(),
                                      copy_bitmask(needles),
                                      needles.null_count(),
                                      stream,
                                      mr);
    if (needles.is_empty()) { return result; }

    auto const out_begin = result->mutable_view().template begin<bool>();
    if (haystack.is_empty()) {
      thrust::uninitialized_fill(
        rmm::exec_policy(stream), out_begin, out_begin + needles.size(), false);
      return result;
    }

    auto const haystack_set    = cudf::detail::unordered_multiset<Type>::create(haystack, stream);
    auto const needles_cdv_ptr = column_device_view::create(needles, stream);
    auto const begin           = thrust::make_counting_iterator<size_type>(0);
    auto const end             = begin + needles.size();

    if (needles.has_nulls()) {
      thrust::transform(
        rmm::exec_policy(stream),
        begin,
        end,
        out_begin,
        [haystack = haystack_set.to_device(), needles = *needles_cdv_ptr] __device__(size_t idx) {
          return needles.is_null_nocheck(idx) ||
                 haystack.contains(needles.template element<Type>(idx));
        });
    } else {
      thrust::transform(
        rmm::exec_policy(stream),
        begin,
        end,
        out_begin,
        [haystack = haystack_set.to_device(), needles = *needles_cdv_ptr] __device__(size_t index) {
          return haystack.contains(needles.template element<Type>(index));
        });
    }

    return result;
  }

  template <typename Type>
  std::enable_if_t<is_nested<Type>(), std::unique_ptr<column>> operator()(
    column_view const& haystack,
    column_view const& needles,
    rmm::cuda_stream_view stream,
    rmm::mr::device_memory_resource* mr) const
  {
    return multi_contains_nested_elements(haystack, needles, stream, mr);
  }
};

template <>
std::unique_ptr<column> multi_contains_dispatch::operator()<dictionary32>(
  column_view const& haystack_in,
  column_view const& needles_in,
  rmm::cuda_stream_view stream,
  rmm::mr::device_memory_resource* mr) const
{
  dictionary_column_view const haystack(haystack_in);
  dictionary_column_view const needles(needles_in);
  // first combine keys so both dictionaries have the same set
  auto needles_matched     = dictionary::detail::add_keys(needles, haystack.keys(), stream);
  auto const needles_view  = dictionary_column_view(needles_matched->view());
  auto haystack_matched    = dictionary::detail::set_keys(haystack, needles_view.keys(), stream);
  auto const haystack_view = dictionary_column_view(haystack_matched->view());

  // now just use the indices for the contains
  column_view const haystack_indices = haystack_view.get_indices_annotated();
  column_view const needles_indices  = needles_view.get_indices_annotated();
  return cudf::type_dispatcher(haystack_indices.type(),
                               multi_contains_dispatch{},
                               haystack_indices,
                               needles_indices,
                               stream,
                               mr);
}
}  // namespace

bool contains(column_view const& haystack, scalar const& needle, rmm::cuda_stream_view stream)
{
  if (haystack.is_empty()) { return false; }
  if (not needle.is_valid(stream)) { return haystack.has_nulls(); }

  return cudf::type_dispatcher(
    haystack.type(), contains_scalar_dispatch{}, haystack, needle, stream);
}

std::unique_ptr<column> contains(column_view const& haystack,
                                 column_view const& needles,
                                 rmm::cuda_stream_view stream,
                                 rmm::mr::device_memory_resource* mr)
{
  CUDF_EXPECTS(haystack.type() == needles.type(), "DTYPE mismatch");
  return cudf::type_dispatcher(
    haystack.type(), multi_contains_dispatch{}, haystack, needles, stream, mr);
}

}  // namespace detail

bool contains(column_view const& haystack, scalar const& needle)
{
  CUDF_FUNC_RANGE();
  return detail::contains(haystack, needle, rmm::cuda_stream_default);
}

std::unique_ptr<column> contains(column_view const& haystack,
                                 column_view const& needles,
                                 rmm::mr::device_memory_resource* mr)
{
  CUDF_FUNC_RANGE();
  return detail::contains(haystack, needles, rmm::cuda_stream_default, mr);
}

}  // namespace cudf
