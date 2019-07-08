/*
 * Copyright (c) 2019, NVIDIA CORPORATION.
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
// The translation unit for reduction `sum`

#include "reduction_functions.cuh"
#include "reduction_dispatcher.cuh"

gdf_scalar cudf::reduction::sum(gdf_column const& col, gdf_dtype const output_dtype, cudaStream_t stream)
{
    using dispacher = cudf::reduction::simple::element_type_dispatcher<cudf::reduction::op::sum>;
    return cudf::type_dispatcher(col.dtype, dispacher(), col, output_dtype, stream);
}


