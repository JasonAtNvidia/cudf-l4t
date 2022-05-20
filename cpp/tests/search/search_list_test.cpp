/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION.
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

#include <cudf_test/base_fixture.hpp>
#include <cudf_test/column_utilities.hpp>
#include <cudf_test/column_wrapper.hpp>
#include <cudf_test/iterator_utilities.hpp>
#include <cudf_test/type_lists.hpp>

#include <cudf/lists/lists_column_view.hpp>
#include <cudf/scalar/scalar.hpp>
#include <cudf/search.hpp>
#include <cudf/table/table_view.hpp>

using namespace cudf::test::iterators;

using bools_col   = cudf::test::fixed_width_column_wrapper<bool>;
using int32s_col  = cudf::test::fixed_width_column_wrapper<int32_t>;
using structs_col = cudf::test::structs_column_wrapper;
using strings_col = cudf::test::strings_column_wrapper;

constexpr cudf::test::debug_output_level verbosity{cudf::test::debug_output_level::ALL_ERRORS};
constexpr int32_t null{0};  // Mark for null child elements at the current level
constexpr int32_t XXX{0};   // Mark for null elements at all levels

using TestTypes = cudf::test::Concat<cudf::test::IntegralTypesNotBool,
                                     cudf::test::FloatingPointTypes,
                                     cudf::test::DurationTypes,
                                     cudf::test::TimestampTypes>;

//==================================================================================================
template <typename T>
struct TypedListsContainsTestScalarNeedle : public cudf::test::BaseFixture {
};
TYPED_TEST_SUITE(TypedListsContainsTestScalarNeedle, TestTypes);

TYPED_TEST(TypedListsContainsTestScalarNeedle, TrivialInputTests)
{
  using tdata_col = cudf::test::fixed_width_column_wrapper<TypeParam, int32_t>;
  using lists_col = cudf::test::lists_column_wrapper<TypeParam, int32_t>;

  auto const haystack = lists_col{{1, 2}, {1}, {}, {1, 3}, {4}, {1, 1}};

  auto const needle1 = [] {
    auto child = tdata_col{1, 2};
    return cudf::list_scalar(child);
  }();
  auto const needle2 = [] {
    auto child = tdata_col{2, 1};
    return cudf::list_scalar(child);
  }();

  EXPECT_EQ(true, cudf::contains(haystack, needle1));
  EXPECT_EQ(false, cudf::contains(haystack, needle2));
}

#if 0

TYPED_TEST(TypedListsContainsTestScalarNeedle, TrivialInputTests)
{
  using tdata_col = cudf::test::fixed_width_column_wrapper<TypeParam, int32_t>;

  auto const col = [] {
    auto child1 = tdata_col{1, 2, 3};
    auto child2 = tdata_col{4, 5, 6};
    auto child3 = strings_col{"x", "y", "z"};
    return structs_col{{child1, child2, child3}};
  }();

  auto const val1 = [] {
    auto child1 = tdata_col{1};
    auto child2 = tdata_col{4};
    auto child3 = strings_col{"x"};
    return make_struct_scalar(child1, child2, child3);
  }();
  auto const val2 = [] {
    auto child1 = tdata_col{1};
    auto child2 = tdata_col{4};
    auto child3 = strings_col{"a"};
    return make_struct_scalar(child1, child2, child3);
  }();

  EXPECT_EQ(true, cudf::contains(col, val1));
  EXPECT_EQ(false, cudf::contains(col, val2));
}

TYPED_TEST(TypedListsContainsTestScalarNeedle, SlicedColumnInputTests)
{
  using tdata_col = cudf::test::fixed_width_column_wrapper<TypeParam, int32_t>;

  constexpr int32_t dont_care{0};

  auto const col_original = [] {
    auto child1 = tdata_col{dont_care, dont_care, 1, 2, 3, dont_care};
    auto child2 = tdata_col{dont_care, dont_care, 4, 5, 6, dont_care};
    auto child3 = strings_col{"dont_care", "dont_care", "x", "y", "z", "dont_care"};
    return structs_col{{child1, child2, child3}};
  }();
  auto const col = cudf::slice(col_original, {2, 5})[0];

  auto const val1 = [] {
    auto child1 = tdata_col{1};
    auto child2 = tdata_col{4};
    auto child3 = strings_col{"x"};
    return make_struct_scalar(child1, child2, child3);
  }();
  auto const val2 = [] {
    auto child1 = tdata_col{dont_care};
    auto child2 = tdata_col{dont_care};
    auto child3 = strings_col{"dont_care"};
    return make_struct_scalar(child1, child2, child3);
  }();

  EXPECT_EQ(true, cudf::contains(col, val1));
  EXPECT_EQ(false, cudf::contains(col, val2));
}

TYPED_TEST(TypedListsContainsTestScalarNeedle, SimpleInputWithNullsTests)
{
  using tdata_col = cudf::test::fixed_width_column_wrapper<TypeParam, int32_t>;

  constexpr int32_t null{0};

  // Test with nulls at the top level.
  {
    auto const col1 = [] {
      auto child1 = tdata_col{1, null, 3};
      auto child2 = tdata_col{4, null, 6};
      auto child3 = strings_col{"x", "" /*NULL*/, "z"};
      return structs_col{{child1, child2, child3}, null_at(1)};
    }();

    auto const val1 = [] {
      auto child1 = tdata_col{1};
      auto child2 = tdata_col{4};
      auto child3 = strings_col{"x"};
      return make_struct_scalar(child1, child2, child3);
    }();
    auto const val2 = [] {
      auto child1 = tdata_col{1};
      auto child2 = tdata_col{4};
      auto child3 = strings_col{"a"};
      return make_struct_scalar(child1, child2, child3);
    }();
    auto const val3 = [] {
      auto child1 = tdata_col{{null}, null_at(0)};
      auto child2 = tdata_col{{null}, null_at(0)};
      auto child3 = strings_col{{""}, null_at(0)};
      return make_struct_scalar(child1, child2, child3);
    }();

    EXPECT_EQ(true, cudf::contains(col1, val1));
    EXPECT_EQ(false, cudf::contains(col1, val2));
    EXPECT_EQ(false, cudf::contains(col1, val3));
  }

  // Test with nulls at the children level.
  {
    auto const col = [] {
      auto child1 = tdata_col{{1, null, 3}, null_at(1)};
      auto child2 = tdata_col{{4, null, 6}, null_at(1)};
      auto child3 = strings_col{{"" /*NULL*/, "" /*NULL*/, "z"}, nulls_at({0, 1})};
      return structs_col{{child1, child2, child3}};
    }();

    auto const val1 = [] {
      auto child1 = tdata_col{1};
      auto child2 = tdata_col{4};
      auto child3 = strings_col{{"" /*NULL*/}, null_at(0)};
      return make_struct_scalar(child1, child2, child3);
    }();
    auto const val2 = [] {
      auto child1 = tdata_col{1};
      auto child2 = tdata_col{4};
      auto child3 = strings_col{""};
      return make_struct_scalar(child1, child2, child3);
    }();
    auto const val3 = [] {
      auto child1 = tdata_col{{null}, null_at(0)};
      auto child2 = tdata_col{{null}, null_at(0)};
      auto child3 = strings_col{{""}, null_at(0)};
      return make_struct_scalar(child1, child2, child3);
    }();

    EXPECT_EQ(true, cudf::contains(col, val1));
    EXPECT_EQ(false, cudf::contains(col, val2));
    EXPECT_EQ(true, cudf::contains(col, val3));
  }

  // Test with nulls in the input scalar.
  {
    auto const col = [] {
      auto child1 = tdata_col{1, 2, 3};
      auto child2 = tdata_col{4, 5, 6};
      auto child3 = strings_col{"x", "y", "z"};
      return structs_col{{child1, child2, child3}};
    }();

    auto const val1 = [] {
      auto child1 = tdata_col{1};
      auto child2 = tdata_col{4};
      auto child3 = strings_col{"x"};
      return make_struct_scalar(child1, child2, child3);
    }();
    auto const val2 = [] {
      auto child1 = tdata_col{1};
      auto child2 = tdata_col{4};
      auto child3 = strings_col{{"" /*NULL*/}, null_at(0)};
      return make_struct_scalar(child1, child2, child3);
    }();

    EXPECT_EQ(true, cudf::contains(col, val1));
    EXPECT_EQ(false, cudf::contains(col, val2));
  }
}

TYPED_TEST(TypedListsContainsTestScalarNeedle, SlicedInputWithNullsTests)
{
  using tdata_col = cudf::test::fixed_width_column_wrapper<TypeParam, int32_t>;

  constexpr int32_t dont_care{0};
  constexpr int32_t null{0};

  // Test with nulls at the top level.
  {
    auto const col_original = [] {
      auto child1 = tdata_col{dont_care, dont_care, 1, null, 3, dont_care};
      auto child2 = tdata_col{dont_care, dont_care, 4, null, 6, dont_care};
      auto child3 = strings_col{"dont_care", "dont_care", "x", "" /*NULL*/, "z", "dont_care"};
      return structs_col{{child1, child2, child3}, null_at(3)};
    }();
    auto const col = cudf::slice(col_original, {2, 5})[0];

    auto const val1 = [] {
      auto child1 = tdata_col{1};
      auto child2 = tdata_col{4};
      auto child3 = strings_col{"x"};
      return make_struct_scalar(child1, child2, child3);
    }();
    auto const val2 = [] {
      auto child1 = tdata_col{1};
      auto child2 = tdata_col{4};
      auto child3 = strings_col{"a"};
      return make_struct_scalar(child1, child2, child3);
    }();

    EXPECT_EQ(true, cudf::contains(col, val1));
    EXPECT_EQ(false, cudf::contains(col, val2));
  }

  // Test with nulls at the children level.
  {
    auto const col_original = [] {
      auto child1 =
        tdata_col{{dont_care, dont_care /*also NULL*/, 1, null, 3, dont_care}, null_at(3)};
      auto child2 =
        tdata_col{{dont_care, dont_care /*also NULL*/, 4, null, 6, dont_care}, null_at(3)};
      auto child3 = strings_col{
        {"dont_care", "dont_care" /*also NULL*/, "" /*NULL*/, "y", "z", "dont_care"}, null_at(2)};
      return structs_col{{child1, child2, child3}, null_at(1)};
    }();
    auto const col = cudf::slice(col_original, {2, 5})[0];

    auto const val1 = [] {
      auto child1 = tdata_col{1};
      auto child2 = tdata_col{4};
      auto child3 = strings_col{{"x"}, null_at(0)};
      return make_struct_scalar(child1, child2, child3);
    }();
    auto const val2 = [] {
      auto child1 = tdata_col{dont_care};
      auto child2 = tdata_col{dont_care};
      auto child3 = strings_col{"dont_care"};
      return make_struct_scalar(child1, child2, child3);
    }();

    EXPECT_EQ(true, cudf::contains(col, val1));
    EXPECT_EQ(false, cudf::contains(col, val2));
  }
}

//==================================================================================================
template <typename T>
struct TypedListContainsTestColumnNeedles : public cudf::test::BaseFixture {
};

TYPED_TEST_SUITE(TypedListContainsTestColumnNeedles, TestTypes);

TYPED_TEST(TypedListContainsTestColumnNeedles, EmptyInputTest)
{
  using tdata_col = cudf::test::fixed_width_column_wrapper<TypeParam, int32_t>;

  auto const haystack = [] {
    auto child1 = tdata_col{};
    auto child2 = tdata_col{};
    auto child3 = strings_col{};
    return structs_col{{child1, child2, child3}};
  }();

  auto const needles = [] {
    auto child1 = tdata_col{};
    auto child2 = tdata_col{};
    auto child3 = strings_col{};
    return structs_col{{child1, child2, child3}};
  }();

  auto const result   = cudf::contains(haystack, needles);
  auto const expected = bools_col{};
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(expected, *result);
}

TYPED_TEST(TypedListContainsTestColumnNeedles, TrivialInputTest)
{
  using tdata_col = cudf::test::fixed_width_column_wrapper<TypeParam, int32_t>;

  auto const haystack = [] {
    auto child1 = tdata_col{1, 3, 1, 1, 2, 1, 2, 2, 1, 2};
    auto child2 = tdata_col{1, 0, 0, 0, 1, 0, 1, 2, 1, 1};
    return structs_col{{child1, child2}};
  }();

  auto const needles = [] {
    auto child1 = tdata_col{1, 3, 1, 1, 2, 1, 0, 0, 1, 0};
    auto child2 = tdata_col{1, 0, 2, 3, 2, 1, 0, 0, 1, 0};
    return structs_col{{child1, child2}};
  }();

  auto const expected = bools_col{1, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  auto const result   = cudf::contains(haystack, needles);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(expected, *result, verbosity);
}

TYPED_TEST(TypedListContainsTestColumnNeedles, SlicedInputNoNulls)
{
  using tdata_col = cudf::test::fixed_width_column_wrapper<TypeParam, int32_t>;

  constexpr int32_t dont_care{0};

  auto const haystack_original = [] {
    auto child1 = tdata_col{dont_care, dont_care, 1, 3, 1, 1, 2, dont_care};
    auto child2 = tdata_col{dont_care, dont_care, 1, 0, 0, 0, 1, dont_care};
    auto child3 = strings_col{"dont_care", "dont_care", "x", "y", "z", "a", "b", "dont_care"};
    return structs_col{{child1, child2, child3}};
  }();
  auto const haystack = cudf::slice(haystack_original, {2, 7})[0];

  auto const needles_original = [] {
    auto child1 = tdata_col{dont_care, 1, 1, 1, 1, 2, dont_care, dont_care};
    auto child2 = tdata_col{dont_care, 0, 1, 2, 3, 1, dont_care, dont_care};
    auto child3 = strings_col{"dont_care", "z", "x", "z", "a", "b", "dont_care", "dont_care"};
    return structs_col{{child1, child2, child3}};
  }();
  auto const needles = cudf::slice(needles_original, {1, 6})[0];

  auto const expected = bools_col{1, 1, 0, 0, 1};
  auto const result   = cudf::contains(haystack, needles);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(expected, *result, verbosity);
}

TYPED_TEST(TypedListContainsTestColumnNeedles, SlicedInputHavingNulls)
{
  using tdata_col = cudf::test::fixed_width_column_wrapper<TypeParam, int32_t>;

  constexpr int32_t dont_care{0};

  auto const haystack_original = [] {
    auto child1 =
      tdata_col{{dont_care /*null*/, dont_care, 1, null, XXX, 1, 2, null, 2, 2, null, 2, dont_care},
                nulls_at({0, 3, 7, 10})};
    auto child2 =
      tdata_col{{dont_care /*null*/, dont_care, 1, null, XXX, 0, null, 0, 1, 2, 1, 1, dont_care},
                nulls_at({0, 3, 6})};
    return structs_col{{child1, child2}, nulls_at({1, 4})};
  }();
  auto const haystack = cudf::slice(haystack_original, {2, 12})[0];

  auto const needles_original = [] {
    auto child1 =
      tdata_col{{dont_care, XXX, null, 1, 1, 2, XXX, null, 1, 1, null, dont_care, dont_care},
                nulls_at({2, 7, 10})};
    auto child2 =
      tdata_col{{dont_care, XXX, null, 2, 3, 2, XXX, null, null, 1, 0, dont_care, dont_care},
                nulls_at({2, 7, 8})};
    return structs_col{{child1, child2}, nulls_at({1, 6})};
  }();
  auto const needles = cudf::slice(needles_original, {1, 11})[0];

  auto const expected = bools_col{{null, 1, 0, 0, 1, null, 1, 0, 1, 1}, nulls_at({0, 5})};
  auto const result   = cudf::contains(haystack, needles);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(expected, *result, verbosity);
}

TYPED_TEST(TypedListContainsTestColumnNeedles, StructOfLists)
{
  using lists_col = cudf::test::lists_column_wrapper<TypeParam, int32_t>;

  auto const haystack = [] {
    // clang-format off
    auto child1 = lists_col{{1, 2},    {1},       lists_col{0, 1}, lists_col{1, 3}};
    auto child2 = lists_col{{1, 3, 4}, {2, 3, 4}, lists_col{},     lists_col{}};
    // clang-format on
    return structs_col{{child1, child2}};
  }();

  auto const needles = [] {
    // clang-format off
    auto child1 = lists_col{lists_col{1, 2},    lists_col{1},    lists_col{}, lists_col{1, 3}};
    auto child2 = lists_col{lists_col{1, 3, 4}, lists_col{2, 3}, lists_col{},     lists_col{}};
    // clang-format on
    return structs_col{{child1, child2}};
  }();

  auto const expected = bools_col{1, 0, 0, 1};
  auto const result   = cudf::contains(haystack, needles);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(expected, *result, verbosity);
}

#endif

#if 0
template <typename T>
struct TypedListContainsTest : public ContainsTest {
};
TYPED_TEST_SUITE(TypedListContainsTest, ContainsTestTypes);

TYPED_TEST(TypedListContainsTest, ScalarKeyLists)
{
  using tdata_col = cudf::test::fixed_width_column_wrapper<TypeParam, int32_t>;
  using lists_col = cudf::test::lists_column_wrapper<TypeParam, int32_t>;

  auto const lists_no_nulls = lists_col{lists_col{{0, 1, 2},  // list0
                                                  {3, 4, 5},
                                                  {0, 1, 2},
                                                  {9, 0, 1, 3, 1}},
                                        lists_col{{2, 3, 4},  // list1
                                                  {3, 4, 5},
                                                  {8, 9, 0},
                                                  {}},
                                        lists_col{{0, 2, 1},  // list2
                                                  {}}};

  auto const lists_have_nulls = lists_col{lists_col{{{0, 1, 2},  // list0
                                                     {} /*NULL*/,
                                                     {0, 1, 2},
                                                     {9, 0, 1, 3, 1}},
                                                    null_at(1)},
                                          lists_col{{{} /*NULL*/,  // list1
                                                     {3, 4, 5},
                                                     {8, 9, 0},
                                                     {}},
                                                    null_at(0)},
                                          lists_col{{0, 2, 1},  // list2
                                                    {}}};

  auto const key = [] {
    auto const child = tdata_col{0, 1, 2};
    return list_scalar(child);
  }();

  auto const do_test = [&](auto const& lists, bool has_nulls) {
    {
      // CONTAINS
      auto const result   = lists::contains(lists_column_view{lists}, key);
      auto const expected = bools_col{1, 0, 0};
      CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
    }
    {
      // CONTAINS NULLS
      auto const result   = lists::contains_nulls(lists_column_view{lists});
      auto const expected = has_nulls ? bools_col{1, 1, 0} : bools_col{0, 0, 0};
      CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
    }
    {
      // FIND_FIRST
      auto const result   = lists::index_of(lists_column_view{lists}, key, FIND_FIRST);
      auto const expected = int32s_col{0, ABSENT, ABSENT};
      CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
    }
    {
      // FIND_LAST
      auto const result   = lists::index_of(lists_column_view{lists}, key, FIND_LAST);
      auto const expected = int32s_col{2, ABSENT, ABSENT};
      CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
    }
  };

  do_test(lists_no_nulls, false);
  do_test(lists_have_nulls, true);
}

TYPED_TEST(TypedListContainsTest, SlicedListsColumn)
{
  using tdata_col = cudf::test::fixed_width_column_wrapper<TypeParam, int32_t>;
  using lists_col = cudf::test::lists_column_wrapper<TypeParam, int32_t>;

  auto const lists_no_nulls_original = lists_col{lists_col{{0, 0, 0},  // list-2 (don't care)
                                                           {0, 1, 2},
                                                           {0, 1, 2},
                                                           {0, 0, 0}},
                                                 lists_col{{0, 0, 0},  // list-1 (don't care)
                                                           {0, 1, 2},
                                                           {0, 1, 2},
                                                           {0, 0, 0}},
                                                 lists_col{{0, 1, 2},  // list0
                                                           {3, 4, 5},
                                                           {0, 1, 2},
                                                           {9, 0, 1, 3, 1}},
                                                 lists_col{{2, 3, 4},  // list1
                                                           {3, 4, 5},
                                                           {8, 9, 0},
                                                           {}},
                                                 lists_col{{0, 2, 1},  // list2
                                                           {}},
                                                 lists_col{{0, 0, 0},  // list3 (don't care)
                                                           {0, 1, 2},
                                                           {0, 1, 2},
                                                           {0, 0, 0}},
                                                 lists_col{{0, 0, 0},  // list4 (don't care)
                                                           {0, 1, 2},
                                                           {0, 1, 2},
                                                           {0, 0, 0}}};

  auto const lists_have_nulls_original = lists_col{lists_col{{0, 0, 0},  // list-1 (don't care)
                                                             {0, 1, 2},
                                                             {0, 1, 2},
                                                             {0, 0, 0}},
                                                   lists_col{{{0, 1, 2},  // list0
                                                              {} /*NULL*/,
                                                              {0, 1, 2},
                                                              {9, 0, 1, 3, 1}},
                                                             null_at(1)},
                                                   lists_col{{{} /*NULL*/,  // list1
                                                              {3, 4, 5},
                                                              {8, 9, 0},
                                                              {}},
                                                             null_at(0)},
                                                   lists_col{{0, 2, 1},  // list2
                                                             {}},
                                                   lists_col{{0, 0, 0},  // list3 (don't care)
                                                             {0, 1, 2},
                                                             {0, 1, 2},
                                                             {0, 0, 0}}};

  auto const lists_no_nulls   = cudf::slice(lists_no_nulls_original, {2, 5})[0];
  auto const lists_have_nulls = cudf::slice(lists_have_nulls_original, {1, 4})[0];

  auto const key = [] {
    auto const child = tdata_col{0, 1, 2};
    return list_scalar(child);
  }();

  auto const do_test = [&](auto const& lists, bool has_nulls) {
    {
      // CONTAINS
      auto const result   = lists::contains(lists_column_view{lists}, key);
      auto const expected = bools_col{1, 0, 0};
      CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
    }
    {
      // CONTAINS NULLS
      auto const result   = lists::contains_nulls(lists_column_view{lists});
      auto const expected = has_nulls ? bools_col{1, 1, 0} : bools_col{0, 0, 0};
      CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
    }
    {
      // FIND_FIRST
      auto const result   = lists::index_of(lists_column_view{lists}, key, FIND_FIRST);
      auto const expected = int32s_col{0, ABSENT, ABSENT};
      CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
    }
    {
      // FIND_LAST
      auto const result   = lists::index_of(lists_column_view{lists}, key, FIND_LAST);
      auto const expected = int32s_col{2, ABSENT, ABSENT};
      CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
    }
  };

  do_test(lists_no_nulls, false);
  do_test(lists_have_nulls, true);
}

TYPED_TEST(TypedListContainsTest, ColumnKeyLists)
{
  using lists_col     = cudf::test::lists_column_wrapper<TypeParam, int32_t>;
  auto constexpr null = int32_t{0};

  auto const lists_no_nulls = lists_col{lists_col{{0, 0, 2},  // list0
                                                  {3, 4, 5},
                                                  {0, 0, 2},
                                                  {9, 0, 1, 3, 1}},
                                        lists_col{{2, 3, 4},  // list1
                                                  {3, 4, 5},
                                                  {2, 3, 4},
                                                  {}},
                                        lists_col{{0, 2, 0},  // list2
                                                  {0, 2, 0},
                                                  {3, 4, 5},
                                                  {}}};

  auto const lists_have_nulls = lists_col{lists_col{{lists_col{{0, null, 2}, null_at(1)},  // list0
                                                     lists_col{} /*NULL*/,
                                                     lists_col{{0, null, 2}, null_at(1)},
                                                     lists_col{9, 0, 1, 3, 1}},
                                                    null_at(1)},
                                          lists_col{{lists_col{} /*NULL*/,  // list1
                                                     lists_col{3, 4, 5},
                                                     lists_col{2, 3, 4},
                                                     lists_col{}},
                                                    null_at(0)},
                                          lists_col{lists_col{0, 2, 1},  // list2
                                                    lists_col{{0, 2, null}, null_at(2)},
                                                    lists_col{3, 4, 5},
                                                    lists_col{}}};

  auto const key = lists_col{
    lists_col{{0, null, 2}, null_at(1)}, lists_col{2, 3, 4}, lists_col{{0, 2, null}, null_at(2)}};

  auto const do_test = [&](auto const& lists, bool has_nulls) {
    {
      // CONTAINS
      auto const result   = lists::contains(lists_column_view{lists}, key);
      auto const expected = has_nulls ? bools_col{1, 1, 1} : bools_col{0, 1, 0};
      CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
    }
    {
      // CONTAINS NULLS
      auto const result   = lists::contains_nulls(lists_column_view{lists});
      auto const expected = has_nulls ? bools_col{1, 1, 0} : bools_col{0, 0, 0};
      CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
    }
    {
      // FIND_FIRST
      auto const result   = lists::index_of(lists_column_view{lists}, key, FIND_FIRST);
      auto const expected = has_nulls ? int32s_col{0, 2, 1} : int32s_col{ABSENT, 0, ABSENT};
      CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
    }
    {
      // FIND_LAST
      auto const result   = lists::index_of(lists_column_view{lists}, key, FIND_LAST);
      auto const expected = has_nulls ? int32s_col{2, 2, 1} : int32s_col{ABSENT, 2, ABSENT};
      CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
    }
  };

  do_test(lists_no_nulls, false);
  do_test(lists_have_nulls, true);
}

TYPED_TEST(TypedListContainsTest, ColumnKeyWithListsOfStructsNoNulls)
{
  using tdata_col = cudf::test::fixed_width_column_wrapper<TypeParam, int32_t>;

  auto const lists = [] {
    auto child_offsets = int32s_col{0, 3, 6, 9, 14, 17, 20, 23, 23};
    // clang-format off
    auto data1 = tdata_col{0, 0, 2,       //
                           3, 4, 5,       //
                           0, 0, 2,       //
                           9, 0, 1, 3, 1, //
                           0, 2, 0,       //
                           0, 0, 2,       //
                           3, 4, 5        //
                                          //
    };
    auto data2 = tdata_col{10, 10, 12,         //
                           13, 14, 15,         //
                           10, 10, 12,         //
                           19, 10, 11, 13, 11, //
                           10, 12, 10,         //
                           10, 10, 12,         //
                           13, 14, 15          //
                                               //
    };
    // clang-format on
    auto structs = structs_col{{data1, data2}};
    auto child   = make_lists_column(8, child_offsets.release(), structs.release(), 0, {});

    auto offsets = int32s_col{0, 4, 8};
    return make_lists_column(2, offsets.release(), std::move(child), 0, {});
  }();

  auto const key = [] {
    auto data1       = tdata_col{0, 0, 2};
    auto data2       = tdata_col{10, 10, 12};
    auto const child = structs_col{{data1, data2}};
    return list_scalar(child);
  }();

  {
    // CONTAINS
    auto const result   = lists::contains(lists_column_view{lists->view()}, key);
    auto const expected = bools_col{1, 1};
    CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
  }
  {
    // FIND_FIRST
    auto const result   = lists::index_of(lists_column_view{lists->view()}, key, FIND_FIRST);
    auto const expected = int32s_col{0, 1};
    CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
  }
  {
    // FIND_LAST
    auto const result   = lists::index_of(lists_column_view{lists->view()}, key, FIND_LAST);
    auto const expected = int32s_col{2, 1};
    CUDF_TEST_EXPECT_COLUMNS_EQUIVALENT(expected, *result);
  }
}

#endif
