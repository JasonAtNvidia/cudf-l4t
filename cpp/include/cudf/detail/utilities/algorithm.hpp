/*
 * Copyright (c) 2021, NVIDIA CORPORATION.
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

namespace cudf::detail {

template <typename Iterator, typename T, typename BinaryOp>
CUDF_HOST_DEVICE T accumulate(Iterator first, Iterator last, T init, BinaryOp op)
{
  for (; first != last; ++first) {
    init = op(init, *first);
  }
  return init;
}
}  // namespace cudf::detail