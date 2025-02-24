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

#include <cudf/column/column_device_view.cuh>
#include <cudf/column/column_factories.hpp>
#include <cudf/detail/null_mask.hpp>
#include <cudf/detail/nvtx/ranges.hpp>
#include <cudf/detail/utilities/cuda.cuh>
#include <cudf/detail/utilities/device_atomics.cuh>
#include <cudf/scalar/scalar_factories.hpp>
#include <cudf/strings/detail/utilities.hpp>
#include <cudf/strings/find.hpp>
#include <cudf/strings/string_view.cuh>
#include <cudf/strings/strings_column_view.hpp>
#include <cudf/utilities/default_stream.hpp>
#include <cudf/utilities/error.hpp>

#include <rmm/cuda_stream_view.hpp>
#include <rmm/exec_policy.hpp>

#include <thrust/binary_search.h>
#include <thrust/fill.h>
#include <thrust/for_each.h>
#include <thrust/iterator/counting_iterator.h>
#include <thrust/transform.h>

namespace cudf {
namespace strings {
namespace detail {
namespace {
/**
 * @brief Utility to return integer column indicating the position of
 * target string within each string in a strings column.
 *
 * Null string entries return corresponding null output column entries.
 *
 * @tparam FindFunction Returns integer character position value given a string and target.
 *
 * @param strings Strings column to search for target.
 * @param target String to search for in each string in the strings column.
 * @param start First character position to start the search.
 * @param stop Last character position (exclusive) to end the search.
 * @param pfn Functor used for locating `target` in each string.
 * @param stream CUDA stream used for device memory operations and kernel launches.
 * @param mr Device memory resource used to allocate the returned column's device memory.
 * @return New integer column with character position values.
 */
template <typename FindFunction>
std::unique_ptr<column> find_fn(strings_column_view const& strings,
                                string_scalar const& target,
                                size_type start,
                                size_type stop,
                                FindFunction& pfn,
                                rmm::cuda_stream_view stream,
                                rmm::mr::device_memory_resource* mr)
{
  CUDF_EXPECTS(target.is_valid(stream), "Parameter target must be valid.");
  CUDF_EXPECTS(start >= 0, "Parameter start must be positive integer or zero.");
  if ((stop > 0) && (start > stop)) CUDF_FAIL("Parameter start must be less than stop.");
  //
  auto d_target       = string_view(target.data(), target.size());
  auto strings_column = column_device_view::create(strings.parent(), stream);
  auto d_strings      = *strings_column;
  auto strings_count  = strings.size();
  // create output column
  auto results      = make_numeric_column(data_type{type_id::INT32},
                                     strings_count,
                                     cudf::detail::copy_bitmask(strings.parent(), stream, mr),
                                     strings.null_count(),
                                     stream,
                                     mr);
  auto results_view = results->mutable_view();
  auto d_results    = results_view.data<int32_t>();
  // set the position values by evaluating the passed function
  thrust::transform(rmm::exec_policy(stream),
                    thrust::make_counting_iterator<size_type>(0),
                    thrust::make_counting_iterator<size_type>(strings_count),
                    d_results,
                    [d_strings, pfn, d_target, start, stop] __device__(size_type idx) {
                      int32_t position = -1;
                      if (!d_strings.is_null(idx))
                        position = static_cast<int32_t>(
                          pfn(d_strings.element<string_view>(idx), d_target, start, stop));
                      return position;
                    });
  results->set_null_count(strings.null_count());
  return results;
}

}  // namespace

std::unique_ptr<column> find(
  strings_column_view const& strings,
  string_scalar const& target,
  size_type start                     = 0,
  size_type stop                      = -1,
  rmm::cuda_stream_view stream        = cudf::default_stream_value,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_current_device_resource())
{
  auto pfn = [] __device__(
               string_view d_string, string_view d_target, size_type start, size_type stop) {
    size_type length = d_string.length();
    if (d_target.empty()) return start > length ? -1 : start;
    size_type begin = (start > length) ? length : start;
    size_type end   = (stop < 0) || (stop > length) ? length : stop;
    return d_string.find(d_target, begin, end - begin);
  };

  return find_fn(strings, target, start, stop, pfn, stream, mr);
}

std::unique_ptr<column> rfind(
  strings_column_view const& strings,
  string_scalar const& target,
  size_type start                     = 0,
  size_type stop                      = -1,
  rmm::cuda_stream_view stream        = cudf::default_stream_value,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_current_device_resource())
{
  auto pfn = [] __device__(
               string_view d_string, string_view d_target, size_type start, size_type stop) {
    size_type length = d_string.length();
    size_type begin  = (start > length) ? length : start;
    size_type end    = (stop < 0) || (stop > length) ? length : stop;
    if (d_target.empty()) return start > length ? -1 : end;
    return d_string.rfind(d_target, begin, end - begin);
  };

  return find_fn(strings, target, start, stop, pfn, stream, mr);
}

}  // namespace detail

// external APIs

std::unique_ptr<column> find(strings_column_view const& strings,
                             string_scalar const& target,
                             size_type start,
                             size_type stop,
                             rmm::mr::device_memory_resource* mr)
{
  CUDF_FUNC_RANGE();
  return detail::find(strings, target, start, stop, cudf::default_stream_value, mr);
}

std::unique_ptr<column> rfind(strings_column_view const& strings,
                              string_scalar const& target,
                              size_type start,
                              size_type stop,
                              rmm::mr::device_memory_resource* mr)
{
  CUDF_FUNC_RANGE();
  return detail::rfind(strings, target, start, stop, cudf::default_stream_value, mr);
}

namespace detail {
namespace {

/**
 * @brief Threshold to decide on using string or warp parallel functions.
 *
 * If the average byte length of a string in a column exceeds this value then
 * the warp-parallel `contains_warp_fn` function is used.
 * Otherwise, the string-parallel function in `contains_fn` is used.
 *
 * This is only used for the scalar version of `contains()` right now.
 */
constexpr size_type AVG_CHAR_BYTES_THRESHOLD = 64;

/**
 * @brief Check if `d_target` appears in a row in `d_strings`.
 *
 * This executes as a warp per string/row.
 */
struct contains_warp_fn {
  column_device_view const d_strings;
  string_view const d_target;
  bool* d_results;

  __device__ void operator()(std::size_t idx)
  {
    auto const str_idx = static_cast<size_type>(idx / cudf::detail::warp_size);
    if (d_strings.is_null(str_idx)) { return; }
    // get the string for this warp
    auto const d_str = d_strings.element<string_view>(str_idx);
    // each thread of the warp will check just part of the string
    auto found = false;
    for (auto i = static_cast<size_type>(idx % cudf::detail::warp_size);
         !found && (i + d_target.size_bytes()) < d_str.size_bytes();
         i += cudf::detail::warp_size) {
      // check the target matches this part of the d_str data
      if (d_target.compare(d_str.data() + i, d_target.size_bytes()) == 0) { found = true; }
    }
    if (found) { atomicOr(d_results + str_idx, true); }
  }
};

std::unique_ptr<column> contains_warp_parallel(strings_column_view const& input,
                                               string_scalar const& target,
                                               rmm::cuda_stream_view stream,
                                               rmm::mr::device_memory_resource* mr)
{
  CUDF_EXPECTS(target.is_valid(stream), "Parameter target must be valid.");
  auto d_target = string_view(target.data(), target.size());

  // create output column
  auto results = make_numeric_column(data_type{type_id::BOOL8},
                                     input.size(),
                                     cudf::detail::copy_bitmask(input.parent(), stream, mr),
                                     input.null_count(),
                                     stream,
                                     mr);

  // fill the output with `false` unless the `d_target` is empty
  auto results_view = results->mutable_view();
  thrust::fill(rmm::exec_policy(stream),
               results_view.begin<bool>(),
               results_view.end<bool>(),
               d_target.empty());

  if (!d_target.empty()) {
    // launch warp per string
    auto d_strings = column_device_view::create(input.parent(), stream);
    thrust::for_each_n(rmm::exec_policy(stream),
                       thrust::make_counting_iterator<std::size_t>(0),
                       static_cast<std::size_t>(input.size()) * cudf::detail::warp_size,
                       contains_warp_fn{*d_strings, d_target, results_view.data<bool>()});
  }
  results->set_null_count(input.null_count());
  return results;
}

/**
 * @brief Utility to return a bool column indicating the presence of
 * a given target string in a strings column.
 *
 * Null string entries return corresponding null output column entries.
 *
 * @tparam BoolFunction Return bool value given two strings.
 *
 * @param strings Column of strings to check for target.
 * @param target UTF-8 encoded string to check in strings column.
 * @param pfn Returns bool value if target is found in the given string.
 * @param stream CUDA stream used for device memory operations and kernel launches.
 * @param mr Device memory resource used to allocate the returned column's device memory.
 * @return New BOOL column.
 */
template <typename BoolFunction>
std::unique_ptr<column> contains_fn(strings_column_view const& strings,
                                    string_scalar const& target,
                                    BoolFunction pfn,
                                    rmm::cuda_stream_view stream,
                                    rmm::mr::device_memory_resource* mr)
{
  auto strings_count = strings.size();
  if (strings_count == 0) return make_empty_column(type_id::BOOL8);

  CUDF_EXPECTS(target.is_valid(stream), "Parameter target must be valid.");
  if (target.size() == 0)  // empty target string returns true
  {
    auto const true_scalar = make_fixed_width_scalar<bool>(true, stream);
    auto results           = make_column_from_scalar(*true_scalar, strings.size(), stream, mr);
    results->set_null_mask(cudf::detail::copy_bitmask(strings.parent(), stream, mr),
                           strings.null_count());
    return results;
  }

  auto d_target       = string_view(target.data(), target.size());
  auto strings_column = column_device_view::create(strings.parent(), stream);
  auto d_strings      = *strings_column;
  // create output column
  auto results      = make_numeric_column(data_type{type_id::BOOL8},
                                     strings_count,
                                     cudf::detail::copy_bitmask(strings.parent(), stream, mr),
                                     strings.null_count(),
                                     stream,
                                     mr);
  auto results_view = results->mutable_view();
  auto d_results    = results_view.data<bool>();
  // set the bool values by evaluating the passed function
  thrust::transform(rmm::exec_policy(stream),
                    thrust::make_counting_iterator<size_type>(0),
                    thrust::make_counting_iterator<size_type>(strings_count),
                    d_results,
                    [d_strings, pfn, d_target] __device__(size_type idx) {
                      if (!d_strings.is_null(idx))
                        return bool{pfn(d_strings.element<string_view>(idx), d_target)};
                      return false;
                    });
  results->set_null_count(strings.null_count());
  return results;
}

/**
 * @brief Utility to return a bool column indicating the presence of
 * a string targets[i] in strings[i].
 *
 * Null string entries return corresponding null output column entries.
 *
 * @tparam BoolFunction Return bool value given two strings.
 *
 * @param strings Column of strings to check for `targets[i]`.
 * @param targets Column of strings to be checked in `strings[i]``.
 * @param pfn Returns bool value if target is found in the given string.
 * @param stream CUDA stream used for device memory operations and kernel launches.
 * @param mr Device memory resource used to allocate the returned column's device memory.
 * @return New BOOL column.
 */
template <typename BoolFunction>
std::unique_ptr<column> contains_fn(strings_column_view const& strings,
                                    strings_column_view const& targets,
                                    BoolFunction pfn,
                                    rmm::cuda_stream_view stream,
                                    rmm::mr::device_memory_resource* mr)
{
  if (strings.is_empty()) return make_empty_column(type_id::BOOL8);

  CUDF_EXPECTS(targets.size() == strings.size(),
               "strings and targets column must be the same size");

  auto targets_column = column_device_view::create(targets.parent(), stream);
  auto d_targets      = *targets_column;
  auto strings_column = column_device_view::create(strings.parent(), stream);
  auto d_strings      = *strings_column;
  // create output column
  auto results      = make_numeric_column(data_type{type_id::BOOL8},
                                     strings.size(),
                                     cudf::detail::copy_bitmask(strings.parent(), stream, mr),
                                     strings.null_count(),
                                     stream,
                                     mr);
  auto results_view = results->mutable_view();
  auto d_results    = results_view.data<bool>();
  // set the bool values by evaluating the passed function
  thrust::transform(
    rmm::exec_policy(stream),
    thrust::make_counting_iterator<size_type>(0),
    thrust::make_counting_iterator<size_type>(strings.size()),
    d_results,
    [d_strings, pfn, d_targets] __device__(size_type idx) {
      // empty target string returns true
      if (d_targets.is_valid(idx) && d_targets.element<string_view>(idx).length() == 0) {
        return true;
      } else if (!d_strings.is_null(idx) && !d_targets.is_null(idx)) {
        return bool{pfn(d_strings.element<string_view>(idx), d_targets.element<string_view>(idx))};
      } else {
        return false;
      }
    });
  results->set_null_count(strings.null_count());
  return results;
}

}  // namespace

std::unique_ptr<column> contains(
  strings_column_view const& input,
  string_scalar const& target,
  rmm::cuda_stream_view stream,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_current_device_resource())
{
  // use warp parallel when the average string width is greater than the threshold
  if (!input.is_empty() && ((input.chars_size() / input.size()) > AVG_CHAR_BYTES_THRESHOLD)) {
    return contains_warp_parallel(input, target, stream, mr);
  }

  // benchmark measurements showed this to be faster for smaller strings
  auto pfn = [] __device__(string_view d_string, string_view d_target) {
    return d_string.find(d_target) != string_view::npos;
  };
  return contains_fn(input, target, pfn, stream, mr);
}

std::unique_ptr<column> contains(
  strings_column_view const& strings,
  strings_column_view const& targets,
  rmm::cuda_stream_view stream,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_current_device_resource())
{
  auto pfn = [] __device__(string_view d_string, string_view d_target) {
    return d_string.find(d_target) != string_view::npos;
  };
  return contains_fn(strings, targets, pfn, stream, mr);
}

std::unique_ptr<column> starts_with(
  strings_column_view const& strings,
  string_scalar const& target,
  rmm::cuda_stream_view stream,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_current_device_resource())
{
  auto pfn = [] __device__(string_view d_string, string_view d_target) {
    return (d_target.size_bytes() <= d_string.size_bytes()) &&
           (d_target.compare(d_string.data(), d_target.size_bytes()) == 0);
  };
  return contains_fn(strings, target, pfn, stream, mr);
}

std::unique_ptr<column> starts_with(
  strings_column_view const& strings,
  strings_column_view const& targets,
  rmm::cuda_stream_view stream,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_current_device_resource())
{
  auto pfn = [] __device__(string_view d_string, string_view d_target) {
    return (d_target.size_bytes() <= d_string.size_bytes()) &&
           (d_target.compare(d_string.data(), d_target.size_bytes()) == 0);
  };
  return contains_fn(strings, targets, pfn, stream, mr);
}

std::unique_ptr<column> ends_with(
  strings_column_view const& strings,
  string_scalar const& target,
  rmm::cuda_stream_view stream,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_current_device_resource())
{
  auto pfn = [] __device__(string_view d_string, string_view d_target) {
    auto const str_size = d_string.size_bytes();
    auto const tgt_size = d_target.size_bytes();
    return (tgt_size <= str_size) &&
           (d_target.compare(d_string.data() + str_size - tgt_size, tgt_size) == 0);
  };

  return contains_fn(strings, target, pfn, stream, mr);
}

std::unique_ptr<column> ends_with(
  strings_column_view const& strings,
  strings_column_view const& targets,
  rmm::cuda_stream_view stream,
  rmm::mr::device_memory_resource* mr = rmm::mr::get_current_device_resource())
{
  auto pfn = [] __device__(string_view d_string, string_view d_target) {
    auto const str_size = d_string.size_bytes();
    auto const tgt_size = d_target.size_bytes();
    return (tgt_size <= str_size) &&
           (d_target.compare(d_string.data() + str_size - tgt_size, tgt_size) == 0);
  };

  return contains_fn(strings, targets, pfn, stream, mr);
}

}  // namespace detail

// external APIs

std::unique_ptr<column> contains(strings_column_view const& strings,
                                 string_scalar const& target,
                                 rmm::mr::device_memory_resource* mr)
{
  CUDF_FUNC_RANGE();
  return detail::contains(strings, target, cudf::default_stream_value, mr);
}

std::unique_ptr<column> contains(strings_column_view const& strings,
                                 strings_column_view const& targets,
                                 rmm::mr::device_memory_resource* mr)
{
  CUDF_FUNC_RANGE();
  return detail::contains(strings, targets, cudf::default_stream_value, mr);
}

std::unique_ptr<column> starts_with(strings_column_view const& strings,
                                    string_scalar const& target,
                                    rmm::mr::device_memory_resource* mr)
{
  CUDF_FUNC_RANGE();
  return detail::starts_with(strings, target, cudf::default_stream_value, mr);
}

std::unique_ptr<column> starts_with(strings_column_view const& strings,
                                    strings_column_view const& targets,
                                    rmm::mr::device_memory_resource* mr)
{
  CUDF_FUNC_RANGE();
  return detail::starts_with(strings, targets, cudf::default_stream_value, mr);
}

std::unique_ptr<column> ends_with(strings_column_view const& strings,
                                  string_scalar const& target,
                                  rmm::mr::device_memory_resource* mr)
{
  CUDF_FUNC_RANGE();
  return detail::ends_with(strings, target, cudf::default_stream_value, mr);
}

std::unique_ptr<column> ends_with(strings_column_view const& strings,
                                  strings_column_view const& targets,
                                  rmm::mr::device_memory_resource* mr)
{
  CUDF_FUNC_RANGE();
  return detail::ends_with(strings, targets, cudf::default_stream_value, mr);
}

}  // namespace strings
}  // namespace cudf
