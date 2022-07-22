# Copyright (c) 2022, NVIDIA CORPORATION.

import numba
import numpy as np
import pandas as pd
from numba import cuda
from numba.core.typing import signature as nb_signature
from numba.types import CPointer, void

import cudf
from cudf.testing._utils import assert_eq
from strings_udf import ptxpath
from strings_udf._lib.cudf_jit_udf import to_string_view_array
from strings_udf._typing import str_view_arg_handler, string_view


def run_udf_test(data, func, dtype):
    dtype = np.dtype(dtype)
    cudf_column = cudf.Series(data)._column
    str_view_ary = to_string_view_array(cudf_column)

    output_ary = cudf.core.column.column_empty(len(data), dtype=dtype)

    kernel = get_kernel(func, dtype)
    kernel.forall(len(data))(str_view_ary, output_ary)
    got = cudf.Series(output_ary, dtype=dtype)
    expect = pd.Series(data).apply(func)
    assert_eq(expect, got, check_dtype=False)


def get_kernel(func, dtype):
    func = cuda.jit(device=True)(func)

    def execute_function(input_strings, output_col):
        id = cuda.grid(1)
        if id < len(output_col):
            st = input_strings[id]
            result = func(st)
            output_col[id] = result

    outty = numba.np.numpy_support.from_dtype(dtype)
    sig = nb_signature(void, CPointer(string_view), outty[::1])
    kernel = cuda.jit(sig, link=[ptxpath], extensions=[str_view_arg_handler])(
        execute_function
    )

    return kernel
