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

#ifdef GTEST_INCLUDE_GTEST_GTEST_H_
#error "Don't include gtest/gtest.h directly, include cudf_gtest.hpp instead"
#endif

/**
 * @file cudf_gtest.hpp
 * @brief Work around for GTests emulation of variadic templates in
 * @verbatim ::Testing::Types @endverbatim
 *
 * @note Instead of including `gtest/gtest.h`, all libcudf test files should
 * include `cudf_gtest.hpp` instead.
 *
 * Removes the 50 type limit in a type-parameterized test list.
 *
 * Uses macros to rename GTests's emulated variadic template types and then
 * redefines them properly.
 */

// @cond
#define Types      Types_NOT_USED
#define Types0     Types0_NOT_USED
#define TypeList   TypeList_NOT_USED
#define Templates  Templates_NOT_USED
#define Templates0 Templates0_NOT_USED
#include <gtest/internal/gtest-type-util.h>
#undef Types
#undef Types0
#undef TypeList
#undef Templates
#undef Templates0

namespace testing {
template <class... TYPES>
struct Types {
  using type = Types;
};

template <class T, class... TYPES>
struct Types<T, TYPES...> {
  using Head = T;
  using Tail = Types<TYPES...>;

  using type = Types;
};

namespace internal {
using Types0 = Types<>;

template <GTEST_TEMPLATE_... TYPES>
struct Templates {
};

template <GTEST_TEMPLATE_ HEAD, GTEST_TEMPLATE_... TAIL>
struct Templates<HEAD, TAIL...> {
  using Head = internal::TemplateSel<HEAD>;
  using Tail = Templates<TAIL...>;

  using type = Templates;
};

using Templates0 = Templates<>;

template <typename T>
struct TypeList {
  using type = Types<T>;
};

template <class... TYPES>
struct TypeList<Types<TYPES...>> {
  using type = Types<TYPES...>;
};

}  // namespace internal
}  // namespace testing
// @endcond

#include <gmock/gmock.h>
#include <gtest/gtest.h>

/**
 * @brief test macro to be expects `expr` to return cudaSuccess
 *
 * This will stop the test process on failure.
 *
 * @param expr expression to be tested
 */
#define ASSERT_CUDA_SUCCEEDED(expr) ASSERT_EQ(cudaSuccess, expr)
/**
 * @brief test macro to be expects `expr` to return cudaSuccess
 *
 * @param expr expression to be tested
 */
#define EXPECT_CUDA_SUCCEEDED(expr) EXPECT_EQ(cudaSuccess, expr)

/**
 * @brief Utility for testing the expectation that an expression x throws the specified
 * exception whose what() message ends with the msg
 *
 * @param x The expression to test
 * @param exception The exception type to test for
 * @param startswith The start of the expected message
 * @param endswith The end of the expected message
 */
#define EXPECT_THROW_MESSAGE(x, exception, startswith, endswith)    \
  do {                                                              \
    EXPECT_THROW(                                                   \
      {                                                             \
        try {                                                       \
          x;                                                        \
        } catch (const exception& e) {                              \
          ASSERT_NE(nullptr, e.what());                             \
          EXPECT_THAT(e.what(), testing::StartsWith((startswith))); \
          EXPECT_THAT(e.what(), testing::EndsWith((endswith)));     \
          throw;                                                    \
        }                                                           \
      },                                                            \
      exception);                                                   \
  } while (0)

/**
 * @brief test macro to be expected to throw cudf::logic_error with a message
 *
 * @param x The statement to be tested
 * @param msg The message associated with the exception
 */
#define CUDF_EXPECT_THROW_MESSAGE(x, msg) \
  EXPECT_THROW_MESSAGE(x, cudf::logic_error, "cuDF failure at:", msg)

/**
 * @brief test macro to be expected to throw cudf::cuda_error with a message
 *
 * @param x The statement to be tested
 * @param msg The message associated with the exception
 */
#define CUDA_EXPECT_THROW_MESSAGE(x, msg) \
  EXPECT_THROW_MESSAGE(x, cudf::cuda_error, "CUDA error encountered at:", msg)

/**
 * @brief test macro to be expected to throw cudf::fatal_logic_error with a message
 *
 * @param x The statement to be tested
 * @param msg The message associated with the exception
 */
#define FATAL_CUDA_EXPECT_THROW_MESSAGE(x, msg) \
  EXPECT_THROW_MESSAGE(x, cudf::fatal_cuda_error, "Fatal CUDA error encountered at:", msg)

/**
 * @brief test macro to be expected as no exception.
 *
 * The testing is same with EXPECT_NO_THROW() in gtest.
 * It also outputs captured error message, useful for debugging.
 *
 * @param statement The statement to be tested
 */
#define CUDF_EXPECT_NO_THROW(statement)                                                       \
  try {                                                                                       \
    statement;                                                                                \
  } catch (std::exception & e) {                                                              \
    FAIL() << "statement:" << #statement << std::endl << "reason: " << e.what() << std::endl; \
  }

/**
 * @brief test macro comparing for equality of \p lhs and and \p rhs for the first \p size elements.
 */
#define CUDF_TEST_EXPECT_VECTOR_EQUAL(lhs, rhs, size)          \
  do {                                                         \
    for (decltype(size) i = 0; i < size; i++)                  \
      EXPECT_EQ(lhs[i], rhs[i]) << "Mismatch at index #" << i; \
  } while (0)
