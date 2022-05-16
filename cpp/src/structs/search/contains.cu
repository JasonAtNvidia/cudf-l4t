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

#include <cudf/scalar/scalar_device_view.cuh>
#include <cudf/structs/detail/contains.hpp>
#include <cudf/table/row_operators.cuh>
#include <cudf/table/table_device_view.cuh>
#include <cudf/table/table_view.hpp>

#include <cudf/table/experimental/row_operators.cuh>

#include <rmm/cuda_stream_view.hpp>
#include <rmm/exec_policy.hpp>

#include <thrust/find.h>
#include <thrust/iterator/counting_iterator.h>

namespace cudf {
namespace structs {
namespace detail {

bool contains(structs_column_view const& haystack,
              scalar const& needle,
              rmm::cuda_stream_view stream)
{
  CUDF_EXPECTS(haystack.type() == needle.type(), "scalar and column types must match");

  auto const needle_tv = static_cast<struct_scalar const*>(&needle)->view();
  CUDF_EXPECTS(haystack.num_children() == needle_tv.num_columns(),
               "struct scalar and structs column must have the same number of children");
  for (size_type i = 0; i < haystack.num_children(); ++i) {
    CUDF_EXPECTS(haystack.child(i).type() == needle_tv.column(i).type(),
                 "scalar and column children types must match");
  }

  // Create a (structs) column_view of one row having children given from the input scalar.
  auto const needle_as_col =
    column_view(data_type{type_id::STRUCT},
                1,
                nullptr,
                nullptr,
                0,
                0,
                std::vector<column_view>{needle_tv.begin(), needle_tv.end()});

  auto const haystack_tv = table_view{{haystack}};
  auto const comparator  = cudf::experimental::row::equality::two_table_comparator(
    haystack_tv, table_view{{needle_as_col}}, stream);
  auto const has_nulls = has_nested_nulls(haystack_tv) || has_nested_nulls(needle_tv);
  auto const d_comp    = comparator.device_comparator(nullate::DYNAMIC{has_nulls});

  auto const start_iter = cudf::experimental::row::lhs_iterator(0);
  auto const end_iter   = start_iter + haystack.size();
  using cudf::experimental::row::rhs_index_type;

  auto const found_iter = thrust::find_if(
    rmm::exec_policy(stream), start_iter, end_iter, [d_comp] __device__(auto const idx) {
      // Compare needle_as_col[0] == haystack[idx].
      return d_comp(static_cast<rhs_index_type>(0), idx);
    });

  return found_iter != end_iter;
}

}  // namespace detail
}  // namespace structs
}  // namespace cudf
