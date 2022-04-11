/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.
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

#include <cudf/detail/iterator.cuh>
#include <cudf/fixed_point/fixed_point.hpp>
#include <cudf/hashing.hpp>

#include <cudf_test/base_fixture.hpp>
#include <cudf_test/column_utilities.hpp>
#include <cudf_test/column_wrapper.hpp>
#include <cudf_test/type_lists.hpp>

using cudf::test::fixed_width_column_wrapper;
using cudf::test::strings_column_wrapper;
using namespace cudf::test;

constexpr debug_output_level verbosity{debug_output_level::ALL_ERRORS};

class HashTest : public cudf::test::BaseFixture {
};

TEST_F(HashTest, MultiValue)
{
  strings_column_wrapper const strings_col({"",
                                            "The quick brown fox",
                                            "jumps over the lazy dog.",
                                            "All work and no play makes Jack a dull boy",
                                            R"(!"#$%&'()*+,-./0123456789:;<=>?@[\]^_`{|}~)"});

  using limits = std::numeric_limits<int32_t>;
  fixed_width_column_wrapper<int32_t> const ints_col({0, 100, -100, limits::min(), limits::max()});

  // Different truth values should be equal
  fixed_width_column_wrapper<bool> const bools_col1({0, 1, 1, 1, 0});
  fixed_width_column_wrapper<bool> const bools_col2({0, 1, 2, 255, 0});

  using ts = cudf::timestamp_s;
  fixed_width_column_wrapper<ts, ts::duration> const secs_col({ts::duration::zero(),
                                                               static_cast<ts::duration>(100),
                                                               static_cast<ts::duration>(-100),
                                                               ts::duration::min(),
                                                               ts::duration::max()});

  auto const input1 = cudf::table_view({strings_col, ints_col, bools_col1, secs_col});
  auto const input2 = cudf::table_view({strings_col, ints_col, bools_col2, secs_col});

  auto const output1 = cudf::hash(input1);
  auto const output2 = cudf::hash(input2);

  EXPECT_EQ(input1.num_rows(), output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(output1->view(), output2->view());
}

TEST_F(HashTest, MultiValueNulls)
{
  // Nulls with different values should be equal
  strings_column_wrapper const strings_col1({"",
                                             "The quick brown fox",
                                             "jumps over the lazy dog.",
                                             "All work and no play makes Jack a dull boy",
                                             R"(!"#$%&'()*+,-./0123456789:;<=>?@[\]^_`{|}~)"},
                                            {0, 1, 1, 0, 1});
  strings_column_wrapper const strings_col2({"different but null",
                                             "The quick brown fox",
                                             "jumps over the lazy dog.",
                                             "I am Jack's complete lack of null value",
                                             R"(!"#$%&'()*+,-./0123456789:;<=>?@[\]^_`{|}~)"},
                                            {0, 1, 1, 0, 1});

  // Nulls with different values should be equal
  using limits = std::numeric_limits<int32_t>;
  fixed_width_column_wrapper<int32_t> const ints_col1({0, 100, -100, limits::min(), limits::max()},
                                                      {1, 0, 0, 1, 1});
  fixed_width_column_wrapper<int32_t> const ints_col2({0, -200, 200, limits::min(), limits::max()},
                                                      {1, 0, 0, 1, 1});

  // Nulls with different values should be equal
  // Different truth values should be equal
  fixed_width_column_wrapper<bool> const bools_col1({0, 1, 0, 1, 1}, {1, 1, 0, 0, 1});
  fixed_width_column_wrapper<bool> const bools_col2({0, 2, 1, 0, 255}, {1, 1, 0, 0, 1});

  // Nulls with different values should be equal
  using ts = cudf::timestamp_s;
  fixed_width_column_wrapper<ts, ts::duration> const secs_col1({ts::duration::zero(),
                                                                static_cast<ts::duration>(100),
                                                                static_cast<ts::duration>(-100),
                                                                ts::duration::min(),
                                                                ts::duration::max()},
                                                               {1, 0, 0, 1, 1});
  fixed_width_column_wrapper<ts, ts::duration> const secs_col2({ts::duration::zero(),
                                                                static_cast<ts::duration>(-200),
                                                                static_cast<ts::duration>(200),
                                                                ts::duration::min(),
                                                                ts::duration::max()},
                                                               {1, 0, 0, 1, 1});

  auto const input1 = cudf::table_view({strings_col1, ints_col1, bools_col1, secs_col1});
  auto const input2 = cudf::table_view({strings_col2, ints_col2, bools_col2, secs_col2});

  auto const output1 = cudf::hash(input1);
  auto const output2 = cudf::hash(input2);

  EXPECT_EQ(input1.num_rows(), output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(output1->view(), output2->view());

  auto const serial_output1 = cudf::hash(input1, cudf::hash_id::HASH_SERIAL_MURMUR3, 0);
  auto const serial_output2 = cudf::hash(input2, cudf::hash_id::HASH_SERIAL_MURMUR3);

  EXPECT_EQ(input1.num_rows(), serial_output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(serial_output1->view(), serial_output2->view());

  auto const spark_output1 = cudf::hash(input1, cudf::hash_id::HASH_SPARK_MURMUR3, 0);
  auto const spark_output2 = cudf::hash(input2, cudf::hash_id::HASH_SPARK_MURMUR3);

  EXPECT_EQ(input1.num_rows(), spark_output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(spark_output1->view(), spark_output2->view());
}

TEST_F(HashTest, BasicList)
{
  using lcw = cudf::test::lists_column_wrapper<uint64_t>;
  using icw = cudf::test::fixed_width_column_wrapper<int32_t>;

  auto const col = lcw{{}, {}, {1}, {1, 1}, {1}, {1, 2}, {2, 2}, {2}, {2}, {2, 1}, {2, 2}, {2, 2}};
  auto const input  = cudf::table_view({col});
  auto const expect = icw{1607593296,
                          1607593296,
                          -636010097,
                          -132459357,
                          -636010097,
                          -2008850957,
                          -1023787369,
                          761197503,
                          761197503,
                          1340177511,
                          -1023787369,
                          -1023787369};

  auto const output = cudf::hash(input);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(expect, output->view(), verbosity);
}

TEST_F(HashTest, NullableList)
{
  using lcw  = cudf::test::lists_column_wrapper<uint64_t>;
  using icw  = cudf::test::fixed_width_column_wrapper<cudf::size_type>;
  using mask = std::vector<bool>;

  auto const valids = mask{1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0};
  auto const col =
    lcw{{{}, {}, {1}, {1}, {2, 2}, {2}, {2}, {}, {2, 2}, {2, 2}, {}}, valids.begin()};
  auto expect = icw{-2023148619,
                    -2023148619,
                    -31671896,
                    -31671896,
                    -1205248335,
                    1865773848,
                    1865773848,
                    -2023148682,
                    -1205248335,
                    -1205248335,
                    -2023148682};

  auto const output = cudf::hash(cudf::table_view({col}));
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(expect, output->view(), verbosity);
}

TEST_F(HashTest, ListOfStruct)
{
  auto col1 = cudf::test::fixed_width_column_wrapper<int32_t>{
    {-1, -1, 0, 2, 2, 2, 1, 2, 0, 2, 0, 2, 0, 2, 0, 0, 1, 2},
    {1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0}};
  auto col2 = cudf::test::strings_column_wrapper{
    {"x", "x", "a", "a", "b", "b", "a", "b", "a", "b", "a", "c", "a", "c", "a", "c", "b", "b"},
    {1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1}};
  auto struc = cudf::test::structs_column_wrapper{
    {col1, col2}, {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}};

  auto offsets = cudf::test::fixed_width_column_wrapper<cudf::size_type>{
    0, 0, 0, 0, 0, 2, 3, 4, 5, 6, 8, 10, 12, 14, 15, 16, 17, 18};

  auto list_nullmask = std::vector<bool>{1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  auto nullmask_buf =
    cudf::test::detail::make_null_mask(list_nullmask.begin(), list_nullmask.end());
  auto list_column = cudf::make_lists_column(
    17, offsets.release(), struc.release(), cudf::UNKNOWN_NULL_COUNT, std::move(nullmask_buf));

  auto expect = cudf::test::fixed_width_column_wrapper<int32_t>{83451479,
                                                                83451479,
                                                                83455332,
                                                                83455332,
                                                                -759684425,
                                                                -959632766,
                                                                -959632766,
                                                                -959632766,
                                                                -959636527,
                                                                -656998704,
                                                                613652814,
                                                                1902080426,
                                                                1902080426,
                                                                2061025592,
                                                                2061025592,
                                                                -319840811,
                                                                -319840811};

  auto const output = cudf::hash(cudf::table_view({*list_column}));
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(expect, output->view(), verbosity);
}

template <typename T>
class HashTestTyped : public cudf::test::BaseFixture {
};

TYPED_TEST_SUITE(HashTestTyped, cudf::test::FixedWidthTypes);

TYPED_TEST(HashTestTyped, Equality)
{
  fixed_width_column_wrapper<TypeParam, int32_t> const col{0, 127, 1, 2, 8};
  auto const input = cudf::table_view({col});

  // Hash of same input should be equal
  auto const output1 = cudf::hash(input);
  auto const output2 = cudf::hash(input);

  EXPECT_EQ(input.num_rows(), output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(output1->view(), output2->view());

  auto const serial_output1 = cudf::hash(input, cudf::hash_id::HASH_SERIAL_MURMUR3, 0);
  auto const serial_output2 = cudf::hash(input, cudf::hash_id::HASH_SERIAL_MURMUR3);

  EXPECT_EQ(input.num_rows(), serial_output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(serial_output1->view(), serial_output2->view());

  auto const spark_output1 = cudf::hash(input, cudf::hash_id::HASH_SPARK_MURMUR3, 0);
  auto const spark_output2 = cudf::hash(input, cudf::hash_id::HASH_SPARK_MURMUR3);

  EXPECT_EQ(input.num_rows(), spark_output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(spark_output1->view(), spark_output2->view());
}

TYPED_TEST(HashTestTyped, EqualityNulls)
{
  using T = TypeParam;

  // Nulls with different values should be equal
  fixed_width_column_wrapper<T, int32_t> const col1({0, 127, 1, 2, 8}, {0, 1, 1, 1, 1});
  fixed_width_column_wrapper<T, int32_t> const col2({1, 127, 1, 2, 8}, {0, 1, 1, 1, 1});

  auto const input1 = cudf::table_view({col1});
  auto const input2 = cudf::table_view({col2});

  auto const output1 = cudf::hash(input1);
  auto const output2 = cudf::hash(input2);

  EXPECT_EQ(input1.num_rows(), output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(output1->view(), output2->view());

  auto const serial_output1 = cudf::hash(input1, cudf::hash_id::HASH_SERIAL_MURMUR3, 0);
  auto const serial_output2 = cudf::hash(input2, cudf::hash_id::HASH_SERIAL_MURMUR3);

  EXPECT_EQ(input1.num_rows(), serial_output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(serial_output1->view(), serial_output2->view());

  auto const spark_output1 = cudf::hash(input1, cudf::hash_id::HASH_SPARK_MURMUR3, 0);
  auto const spark_output2 = cudf::hash(input2, cudf::hash_id::HASH_SPARK_MURMUR3);

  EXPECT_EQ(input1.num_rows(), spark_output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(spark_output1->view(), spark_output2->view());
}

template <typename T>
class HashTestFloatTyped : public cudf::test::BaseFixture {
};

TYPED_TEST_SUITE(HashTestFloatTyped, cudf::test::FloatingPointTypes);

TYPED_TEST(HashTestFloatTyped, TestExtremes)
{
  using T = TypeParam;
  T min   = std::numeric_limits<T>::min();
  T max   = std::numeric_limits<T>::max();
  T nan   = std::numeric_limits<T>::quiet_NaN();
  T inf   = std::numeric_limits<T>::infinity();

  fixed_width_column_wrapper<T> const col({T(0.0), T(100.0), T(-100.0), min, max, nan, inf, -inf});
  fixed_width_column_wrapper<T> const col_neg_zero(
    {T(-0.0), T(100.0), T(-100.0), min, max, nan, inf, -inf});
  fixed_width_column_wrapper<T> const col_neg_nan(
    {T(0.0), T(100.0), T(-100.0), min, max, -nan, inf, -inf});

  auto const table_col          = cudf::table_view({col});
  auto const table_col_neg_zero = cudf::table_view({col_neg_zero});
  auto const table_col_neg_nan  = cudf::table_view({col_neg_nan});

  auto const hash_col          = cudf::hash(table_col);
  auto const hash_col_neg_zero = cudf::hash(table_col_neg_zero);
  auto const hash_col_neg_nan  = cudf::hash(table_col_neg_nan);

  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_col, *hash_col_neg_zero, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_col, *hash_col_neg_nan, verbosity);

  constexpr auto serial_hasher   = cudf::hash_id::HASH_SERIAL_MURMUR3;
  auto const serial_col          = cudf::hash(table_col, serial_hasher, 0);
  auto const serial_col_neg_zero = cudf::hash(table_col_neg_zero, serial_hasher);
  auto const serial_col_neg_nan  = cudf::hash(table_col_neg_nan, serial_hasher);

  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*serial_col, *serial_col_neg_zero, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*serial_col, *serial_col_neg_nan, verbosity);

  // Spark hash is sensitive to 0 and -0
  constexpr auto spark_hasher  = cudf::hash_id::HASH_SPARK_MURMUR3;
  auto const spark_col         = cudf::hash(table_col, spark_hasher, 0);
  auto const spark_col_neg_nan = cudf::hash(table_col_neg_nan, spark_hasher);

  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*spark_col, *spark_col_neg_nan);
}

class SerialMurmurHash3Test : public cudf::test::BaseFixture {
};

TEST_F(SerialMurmurHash3Test, MultiValueWithSeeds)
{
  fixed_width_column_wrapper<int32_t> const strings_col_result(
    {1467149710, -680899318, -1620282500, 91106683, -1564993834});
  fixed_width_column_wrapper<int32_t> const ints_col_result(
    {933211791, 751823303, -1080202046, 723455942, 133916647});

  strings_column_wrapper const strings_col({"",
                                            "The quick brown fox",
                                            "jumps over the lazy dog.",
                                            "All work and no play makes Jack a dull boy",
                                            "!\"#$%&\'()*+,-./]:;<=>?@[\\]^_`{|}~\ud720\ud721"});

  using limits = std::numeric_limits<int32_t>;
  fixed_width_column_wrapper<int32_t> const ints_col({0, 100, -100, limits::min(), limits::max()});

  fixed_width_column_wrapper<bool> const bools_col1({0, 1, 1, 1, 0});
  fixed_width_column_wrapper<bool> const bools_col2({0, 1, 2, 255, 0});

  std::vector<std::unique_ptr<cudf::column>> struct_field_cols;
  struct_field_cols.emplace_back(std::make_unique<cudf::column>(strings_col));
  struct_field_cols.emplace_back(std::make_unique<cudf::column>(ints_col));
  struct_field_cols.emplace_back(std::make_unique<cudf::column>(bools_col1));
  structs_column_wrapper structs_col(std::move(struct_field_cols));

  auto const combo1 = cudf::table_view({strings_col, ints_col, bools_col1});
  auto const combo2 = cudf::table_view({strings_col, ints_col, bools_col2});

  constexpr auto hasher   = cudf::hash_id::HASH_SERIAL_MURMUR3;
  auto const strings_hash = cudf::hash(cudf::table_view({strings_col}), hasher, 314);
  auto const ints_hash    = cudf::hash(cudf::table_view({ints_col}), hasher, 42);
  auto const combo1_hash  = cudf::hash(combo1, hasher, {});
  auto const combo2_hash  = cudf::hash(combo2, hasher, {});
  auto const structs_hash = cudf::hash(cudf::table_view({structs_col}), hasher, {});

  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*strings_hash, strings_col_result, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*ints_hash, ints_col_result, verbosity);
  EXPECT_EQ(combo1.num_rows(), combo1_hash->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*combo1_hash, *combo2_hash, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*structs_hash, *combo1_hash, verbosity);
}

TEST_F(SerialMurmurHash3Test, ListThrows)
{
  lists_column_wrapper<cudf::string_view> strings_list_col({{""}, {"abc"}, {"123"}});
  EXPECT_THROW(
    cudf::hash(cudf::table_view({strings_list_col}), cudf::hash_id::HASH_SERIAL_MURMUR3, {}),
    cudf::logic_error);
}

class SparkMurmurHash3Test : public cudf::test::BaseFixture {
};

TEST_F(SparkMurmurHash3Test, MultiValueWithSeeds)
{
  // The hash values were determined by running the following Scala code in Apache Spark:
  // import org.apache.spark.sql.catalyst.util.DateTimeUtils
  // val schema = new StructType().add("structs", new StructType().add("a",IntegerType)
  //   .add("b",StringType).add("c",new StructType().add("x",FloatType).add("y",LongType)))
  //   .add("strings",StringType).add("doubles",DoubleType).add("timestamps",TimestampType)
  //   .add("decimal64", DecimalType(18,7)).add("longs",LongType).add("floats",FloatType)
  //   .add("dates",DateType).add("decimal32", DecimalType(9,3)).add("ints",IntegerType)
  //   .add("shorts",ShortType).add("bytes",ByteType).add("bools",BooleanType)
  //   .add("decimal128", DecimalType(38,11))
  // val data = Seq(
  // Row(Row(0, "a", Row(0f, 0L)), "", 0.toDouble, DateTimeUtils.toJavaTimestamp(0), BigDecimal(0),
  //     0.toLong, 0.toFloat, DateTimeUtils.toJavaDate(0), BigDecimal(0), 0, 0.toShort, 0.toByte,
  //     false, BigDecimal(0)),
  // Row(Row(100, "bc", Row(100f, 100L)), "The quick brown fox", -(0.toDouble),
  //     DateTimeUtils.toJavaTimestamp(100), BigDecimal("0.00001"), 100.toLong, -(0.toFloat),
  //     DateTimeUtils.toJavaDate(100), BigDecimal("0.1"), 100, 100.toShort, 100.toByte, true,
  //     BigDecimal("0.000000001")),
  // Row(Row(-100, "def", Row(-100f, -100L)), "jumps over the lazy dog.", -Double.NaN,
  //     DateTimeUtils.toJavaTimestamp(-100), BigDecimal("-0.00001"), -100.toLong, -Float.NaN,
  //     DateTimeUtils.toJavaDate(-100), BigDecimal("-0.1"), -100, -100.toShort, -100.toByte,
  //     true, BigDecimal("-0.00000000001")),
  // Row(Row(0x12345678, "ghij", Row(Float.PositiveInfinity, 0x123456789abcdefL)),
  //     "All work and no play makes Jack a dull boy", Double.MinValue,
  //     DateTimeUtils.toJavaTimestamp(Long.MinValue/1000000), BigDecimal("-99999999999.9999999"),
  //     Long.MinValue, Float.MinValue, DateTimeUtils.toJavaDate(Int.MinValue/100),
  //     BigDecimal("-999999.999"), Int.MinValue, Short.MinValue, Byte.MinValue, true,
  //     BigDecimal("-9999999999999999.99999999999")),
  // Row(Row(-0x76543210, "klmno", Row(Float.NegativeInfinity, -0x123456789abcdefL)),
  //     "!\"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~\ud720\ud721", Double.MaxValue,
  //     DateTimeUtils.toJavaTimestamp(Long.MaxValue/1000000), BigDecimal("99999999999.9999999"),
  //     Long.MaxValue, Float.MaxValue, DateTimeUtils.toJavaDate(Int.MaxValue/100),
  //     BigDecimal("999999.999"), Int.MaxValue, Short.MaxValue, Byte.MaxValue, false,
  //     BigDecimal("99999999999999999999999999.99999999999")))
  // val df = spark.createDataFrame(sc.parallelize(data), schema)
  // df.columns.foreach(c => println(s"$c => ${df.select(hash(col(c))).collect.mkString(",")}"))
  // df.select(hash(col("*"))).collect
  fixed_width_column_wrapper<int32_t> const hash_structs_expected(
    {-105406170, 90479889, -678041645, 1667387937, 301478567});
  fixed_width_column_wrapper<int32_t> const hash_strings_expected(
    {1467149710, 723257560, -1620282500, -2001858707, 1588473657});
  fixed_width_column_wrapper<int32_t> const hash_doubles_expected(
    {-1670924195, -853646085, -1281358385, 1897734433, -508695674});
  fixed_width_column_wrapper<int32_t> const hash_timestamps_expected(
    {-1670924195, 1114849490, 904948192, -1832979433, 1752430209});
  fixed_width_column_wrapper<int32_t> const hash_decimal64_expected(
    {-1670924195, 1114849490, 904948192, 1962370902, -1795328666});
  fixed_width_column_wrapper<int32_t> const hash_longs_expected(
    {-1670924195, 1114849490, 904948192, -853646085, -1604625029});
  fixed_width_column_wrapper<int32_t> const hash_floats_expected(
    {933211791, 723455942, -349261430, -1225560532, -338752985});
  fixed_width_column_wrapper<int32_t> const hash_dates_expected(
    {933211791, 751823303, -1080202046, -1906567553, -1503850410});
  fixed_width_column_wrapper<int32_t> const hash_decimal32_expected(
    {-1670924195, 1114849490, 904948192, -1454351396, -193774131});
  fixed_width_column_wrapper<int32_t> const hash_ints_expected(
    {933211791, 751823303, -1080202046, 723455942, 133916647});
  fixed_width_column_wrapper<int32_t> const hash_shorts_expected(
    {933211791, 751823303, -1080202046, -1871935946, 1249274084});
  fixed_width_column_wrapper<int32_t> const hash_bytes_expected(
    {933211791, 751823303, -1080202046, 1110053733, 1135925485});
  fixed_width_column_wrapper<int32_t> const hash_bools_expected(
    {933211791, -559580957, -559580957, -559580957, 933211791});
  fixed_width_column_wrapper<int32_t> const hash_decimal128_expected(
    {-783713497, -295670906, 1398487324, -52622807, -1359749815});
  fixed_width_column_wrapper<int32_t> const hash_combined_expected(
    {401603227, 588162166, 552160517, 1132537411, -326043017});

  using double_limits = std::numeric_limits<double>;
  using long_limits   = std::numeric_limits<int64_t>;
  using float_limits  = std::numeric_limits<float>;
  using int_limits    = std::numeric_limits<int32_t>;
  fixed_width_column_wrapper<int32_t> a_col{0, 100, -100, 0x12345678, -0x76543210};
  strings_column_wrapper b_col{"a", "bc", "def", "ghij", "klmno"};
  fixed_width_column_wrapper<float> x_col{
    0.f, 100.f, -100.f, float_limits::infinity(), -float_limits::infinity()};
  fixed_width_column_wrapper<int64_t> y_col{
    0L, 100L, -100L, 0x123456789abcdefL, -0x123456789abcdefL};
  structs_column_wrapper c_col{{x_col, y_col}};
  structs_column_wrapper const structs_col{{a_col, b_col, c_col}};

  strings_column_wrapper const strings_col({"",
                                            "The quick brown fox",
                                            "jumps over the lazy dog.",
                                            "All work and no play makes Jack a dull boy",
                                            "!\"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~\ud720\ud721"});
  fixed_width_column_wrapper<double> const doubles_col(
    {0., -0., -double_limits::quiet_NaN(), double_limits::lowest(), double_limits::max()});
  fixed_width_column_wrapper<cudf::timestamp_ms, cudf::timestamp_ms::rep> const timestamps_col(
    {0L, 100L, -100L, long_limits::min() / 1000000, long_limits::max() / 1000000});
  fixed_point_column_wrapper<int64_t> const decimal64_col(
    {0L, 100L, -100L, -999999999999999999L, 999999999999999999L}, numeric::scale_type{-7});
  fixed_width_column_wrapper<int64_t> const longs_col(
    {0L, 100L, -100L, long_limits::min(), long_limits::max()});
  fixed_width_column_wrapper<float> const floats_col(
    {0.f, -0.f, -float_limits::quiet_NaN(), float_limits::lowest(), float_limits::max()});
  fixed_width_column_wrapper<cudf::timestamp_D, cudf::timestamp_D::rep> dates_col(
    {0, 100, -100, int_limits::min() / 100, int_limits::max() / 100});
  fixed_point_column_wrapper<int32_t> const decimal32_col({0, 100, -100, -999999999, 999999999},
                                                          numeric::scale_type{-3});
  fixed_width_column_wrapper<int32_t> const ints_col(
    {0, 100, -100, int_limits::min(), int_limits::max()});
  fixed_width_column_wrapper<int16_t> const shorts_col({0, 100, -100, -32768, 32767});
  fixed_width_column_wrapper<int8_t> const bytes_col({0, 100, -100, -128, 127});
  fixed_width_column_wrapper<bool> const bools_col1({0, 1, 1, 1, 0});
  fixed_width_column_wrapper<bool> const bools_col2({0, 1, 2, 255, 0});
  fixed_point_column_wrapper<__int128_t> const decimal128_col(
    {static_cast<__int128>(0),
     static_cast<__int128>(100),
     static_cast<__int128>(-1),
     (static_cast<__int128>(0xFFFFFFFFFCC4D1C3u) << 64 | 0x602F7FC318000001u),
     (static_cast<__int128>(0x0785EE10D5DA46D9u) << 64 | 0x00F4369FFFFFFFFFu)},
    numeric::scale_type{-11});

  constexpr auto hasher      = cudf::hash_id::HASH_SPARK_MURMUR3;
  auto const hash_structs    = cudf::hash(cudf::table_view({structs_col}), hasher, 42);
  auto const hash_strings    = cudf::hash(cudf::table_view({strings_col}), hasher, 314);
  auto const hash_doubles    = cudf::hash(cudf::table_view({doubles_col}), hasher, 42);
  auto const hash_timestamps = cudf::hash(cudf::table_view({timestamps_col}), hasher, 42);
  auto const hash_decimal64  = cudf::hash(cudf::table_view({decimal64_col}), hasher, 42);
  auto const hash_longs      = cudf::hash(cudf::table_view({longs_col}), hasher, 42);
  auto const hash_floats     = cudf::hash(cudf::table_view({floats_col}), hasher, 42);
  auto const hash_dates      = cudf::hash(cudf::table_view({dates_col}), hasher, 42);
  auto const hash_decimal32  = cudf::hash(cudf::table_view({decimal32_col}), hasher, 42);
  auto const hash_ints       = cudf::hash(cudf::table_view({ints_col}), hasher, 42);
  auto const hash_shorts     = cudf::hash(cudf::table_view({shorts_col}), hasher, 42);
  auto const hash_bytes      = cudf::hash(cudf::table_view({bytes_col}), hasher, 42);
  auto const hash_bools1     = cudf::hash(cudf::table_view({bools_col1}), hasher, 42);
  auto const hash_bools2     = cudf::hash(cudf::table_view({bools_col2}), hasher, 42);
  auto const hash_decimal128 = cudf::hash(cudf::table_view({decimal128_col}), hasher, 42);

  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_structs, hash_structs_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_strings, hash_strings_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_doubles, hash_doubles_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_timestamps, hash_timestamps_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_decimal64, hash_decimal64_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_longs, hash_longs_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_floats, hash_floats_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_dates, hash_dates_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_decimal32, hash_decimal32_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_ints, hash_ints_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_shorts, hash_shorts_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_bytes, hash_bytes_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_bools1, hash_bools_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_bools2, hash_bools_expected, verbosity);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_decimal128, hash_decimal128_expected, verbosity);

  auto const combined_table = cudf::table_view({structs_col,
                                                strings_col,
                                                doubles_col,
                                                timestamps_col,
                                                decimal64_col,
                                                longs_col,
                                                floats_col,
                                                dates_col,
                                                decimal32_col,
                                                ints_col,
                                                shorts_col,
                                                bytes_col,
                                                bools_col2,
                                                decimal128_col});
  auto const hash_combined  = cudf::hash(combined_table, hasher, 42);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(*hash_combined, hash_combined_expected, verbosity);
}

TEST_F(SparkMurmurHash3Test, ListThrows)
{
  lists_column_wrapper<cudf::string_view> strings_list_col({{""}, {"abc"}, {"123"}});
  EXPECT_THROW(
    cudf::hash(cudf::table_view({strings_list_col}), cudf::hash_id::HASH_SPARK_MURMUR3, {}),
    cudf::logic_error);
}

class MD5HashTest : public cudf::test::BaseFixture {
};

TEST_F(MD5HashTest, MultiValue)
{
  strings_column_wrapper const strings_col(
    {"",
     "A 60 character string to test MD5's message padding algorithm",
     "A very long (greater than 128 bytes/char string) to test a multi hash-step data point in the "
     "MD5 hash function. This string needed to be longer.",
     "All work and no play makes Jack a dull boy",
     R"(!"#$%&'()*+,-./0123456789:;<=>?@[\]^_`{|}~)"});

  strings_column_wrapper const md5_string_results1({"d41d8cd98f00b204e9800998ecf8427e",
                                                    "682240021651ae166d08fe2a014d5c09",
                                                    "3669d5225fddbb34676312ca3b78bbd9",
                                                    "c61a4185135eda043f35e92c3505e180",
                                                    "52da74c75cb6575d25be29e66bd0adde"});

  strings_column_wrapper const md5_string_results2({"d41d8cd98f00b204e9800998ecf8427e",
                                                    "e5a5682e82278e78dbaad9a689df7a73",
                                                    "4121ab1bb6e84172fd94822645862ae9",
                                                    "28970886501efe20164213855afe5850",
                                                    "6bc1b872103cc6a02d882245b8516e2e"});

  using limits = std::numeric_limits<int32_t>;
  fixed_width_column_wrapper<int32_t> const ints_col({0, 100, -100, limits::min(), limits::max()});

  // Different truth values should be equal
  fixed_width_column_wrapper<bool> const bools_col1({0, 1, 1, 1, 0});
  fixed_width_column_wrapper<bool> const bools_col2({0, 1, 2, 255, 0});

  auto const string_input1      = cudf::table_view({strings_col});
  auto const string_input2      = cudf::table_view({strings_col, strings_col});
  auto const md5_string_output1 = cudf::hash(string_input1, cudf::hash_id::HASH_MD5);
  auto const md5_string_output2 = cudf::hash(string_input2, cudf::hash_id::HASH_MD5);
  EXPECT_EQ(string_input1.num_rows(), md5_string_output1->size());
  EXPECT_EQ(string_input2.num_rows(), md5_string_output2->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(md5_string_output1->view(), md5_string_results1);
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(md5_string_output2->view(), md5_string_results2);

  auto const input1      = cudf::table_view({strings_col, ints_col, bools_col1});
  auto const input2      = cudf::table_view({strings_col, ints_col, bools_col2});
  auto const md5_output1 = cudf::hash(input1, cudf::hash_id::HASH_MD5);
  auto const md5_output2 = cudf::hash(input2, cudf::hash_id::HASH_MD5);
  EXPECT_EQ(input1.num_rows(), md5_output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(md5_output1->view(), md5_output2->view());
}

TEST_F(MD5HashTest, MultiValueNulls)
{
  // Nulls with different values should be equal
  strings_column_wrapper const strings_col1(
    {"",
     "Different but null!",
     "A very long (greater than 128 bytes/char string) to test a multi hash-step data point in the "
     "MD5 hash function. This string needed to be longer.",
     "All work and no play makes Jack a dull boy",
     R"(!"#$%&'()*+,-./0123456789:;<=>?@[\]^_`{|}~)"},
    {1, 0, 0, 1, 0});
  strings_column_wrapper const strings_col2(
    {"",
     "A 60 character string to test MD5's message padding algorithm",
     "Very different... but null",
     "All work and no play makes Jack a dull boy",
     ""},
    {1, 0, 0, 1, 1});  // empty string is equivalent to null

  // Nulls with different values should be equal
  using limits = std::numeric_limits<int32_t>;
  fixed_width_column_wrapper<int32_t> const ints_col1({0, 100, -100, limits::min(), limits::max()},
                                                      {1, 0, 0, 1, 1});
  fixed_width_column_wrapper<int32_t> const ints_col2({0, -200, 200, limits::min(), limits::max()},
                                                      {1, 0, 0, 1, 1});

  // Nulls with different values should be equal
  // Different truth values should be equal
  fixed_width_column_wrapper<bool> const bools_col1({0, 1, 0, 1, 1}, {1, 1, 0, 0, 1});
  fixed_width_column_wrapper<bool> const bools_col2({0, 2, 1, 0, 255}, {1, 1, 0, 0, 1});

  auto const input1 = cudf::table_view({strings_col1, ints_col1, bools_col1});
  auto const input2 = cudf::table_view({strings_col2, ints_col2, bools_col2});

  auto const output1 = cudf::hash(input1, cudf::hash_id::HASH_MD5);
  auto const output2 = cudf::hash(input2, cudf::hash_id::HASH_MD5);

  EXPECT_EQ(input1.num_rows(), output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(output1->view(), output2->view());
}

TEST_F(MD5HashTest, StringListsNulls)
{
  auto validity = cudf::detail::make_counting_transform_iterator(0, [](auto i) { return i != 0; });

  strings_column_wrapper const strings_col(
    {"",
     "A 60 character string to test MD5's message padding algorithm",
     "A very long (greater than 128 bytes/char string) to test a multi hash-step data point in the "
     "MD5 hash function. This string needed to be longer. It needed to be even longer.",
     "All work and no play makes Jack a dull boy",
     R"(!"#$%&'()*+,-./0123456789:;<=>?@[\]^_`{|}~)"});

  lists_column_wrapper<cudf::string_view> strings_list_col(
    {{""},
     {{"NULL", "A 60 character string to test MD5's message padding algorithm"}, validity},
     {"A very long (greater than 128 bytes/char string) to test a multi hash-step data point in "
      "the "
      "MD5 hash function. This string needed to be longer.",
      " It needed to be even longer."},
     {"All ", "work ", "and", " no", " play ", "makes Jack", " a dull boy"},
     {"!\"#$%&\'()*+,-./0123456789:;<=>?@[\\]^_`", "{|}~"}});

  auto const input1 = cudf::table_view({strings_col});
  auto const input2 = cudf::table_view({strings_list_col});

  auto const output1 = cudf::hash(input1, cudf::hash_id::HASH_MD5);
  auto const output2 = cudf::hash(input2, cudf::hash_id::HASH_MD5);

  CUDF_TEST_EXPECT_COLUMNS_EQUAL(output1->view(), output2->view());
}

template <typename T>
class MD5HashTestTyped : public cudf::test::BaseFixture {
};

TYPED_TEST_SUITE(MD5HashTestTyped, cudf::test::NumericTypes);

TYPED_TEST(MD5HashTestTyped, Equality)
{
  fixed_width_column_wrapper<TypeParam> const col({0, 127, 1, 2, 8});
  auto const input = cudf::table_view({col});

  // Hash of same input should be equal
  auto const output1 = cudf::hash(input, cudf::hash_id::HASH_MD5);
  auto const output2 = cudf::hash(input, cudf::hash_id::HASH_MD5);

  EXPECT_EQ(input.num_rows(), output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(output1->view(), output2->view());
}

TYPED_TEST(MD5HashTestTyped, EqualityNulls)
{
  using T = TypeParam;

  // Nulls with different values should be equal
  fixed_width_column_wrapper<T> const col1({0, 127, 1, 2, 8}, {0, 1, 1, 1, 1});
  fixed_width_column_wrapper<T> const col2({1, 127, 1, 2, 8}, {0, 1, 1, 1, 1});

  auto const input1 = cudf::table_view({col1});
  auto const input2 = cudf::table_view({col2});

  auto const output1 = cudf::hash(input1, cudf::hash_id::HASH_MD5);
  auto const output2 = cudf::hash(input2, cudf::hash_id::HASH_MD5);

  EXPECT_EQ(input1.num_rows(), output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(output1->view(), output2->view());
}

TEST_F(MD5HashTest, TestBoolListsWithNulls)
{
  fixed_width_column_wrapper<bool> const col1({0, 255, 255, 16, 27, 18, 100, 1, 2},
                                              {1, 0, 0, 0, 1, 1, 1, 0, 0});
  fixed_width_column_wrapper<bool> const col2({0, 255, 255, 32, 81, 68, 3, 101, 4},
                                              {1, 0, 0, 1, 0, 1, 0, 1, 0});
  fixed_width_column_wrapper<bool> const col3({0, 255, 255, 64, 49, 42, 5, 6, 102},
                                              {1, 0, 0, 1, 1, 0, 0, 0, 1});

  auto validity = cudf::detail::make_counting_transform_iterator(0, [](auto i) { return i != 1; });
  lists_column_wrapper<bool> const list_col(
    {{0, 0, 0}, {1}, {}, {{1, 1, 1}, validity}, {1, 1}, {1, 1}, {1}, {1}, {1}}, validity);

  auto const input1 = cudf::table_view({col1, col2, col3});
  auto const input2 = cudf::table_view({list_col});

  auto const output1 = cudf::hash(input1, cudf::hash_id::HASH_MD5);
  auto const output2 = cudf::hash(input2, cudf::hash_id::HASH_MD5);

  EXPECT_EQ(input1.num_rows(), output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(output1->view(), output2->view());
}

template <typename T>
class MD5HashListTestTyped : public cudf::test::BaseFixture {
};

using NumericTypesNoBools = Concat<IntegralTypesNotBool, FloatingPointTypes>;
TYPED_TEST_SUITE(MD5HashListTestTyped, NumericTypesNoBools);

TYPED_TEST(MD5HashListTestTyped, TestListsWithNulls)
{
  using T = TypeParam;

  fixed_width_column_wrapper<T> const col1({0, 255, 255, 16, 27, 18, 100, 1, 2},
                                           {1, 0, 0, 0, 1, 1, 1, 0, 0});
  fixed_width_column_wrapper<T> const col2({0, 255, 255, 32, 81, 68, 3, 101, 4},
                                           {1, 0, 0, 1, 0, 1, 0, 1, 0});
  fixed_width_column_wrapper<T> const col3({0, 255, 255, 64, 49, 42, 5, 6, 102},
                                           {1, 0, 0, 1, 1, 0, 0, 0, 1});

  auto validity = cudf::detail::make_counting_transform_iterator(0, [](auto i) { return i != 1; });
  lists_column_wrapper<T> const list_col(
    {{0, 0, 0}, {127}, {}, {{32, 127, 64}, validity}, {27, 49}, {18, 68}, {100}, {101}, {102}},
    validity);

  auto const input1 = cudf::table_view({col1, col2, col3});
  auto const input2 = cudf::table_view({list_col});

  auto const output1 = cudf::hash(input1, cudf::hash_id::HASH_MD5);
  auto const output2 = cudf::hash(input2, cudf::hash_id::HASH_MD5);

  EXPECT_EQ(input1.num_rows(), output1->size());
  CUDF_TEST_EXPECT_COLUMNS_EQUAL(output1->view(), output2->view());
}

template <typename T>
class MD5HashTestFloatTyped : public cudf::test::BaseFixture {
};

TYPED_TEST_SUITE(MD5HashTestFloatTyped, cudf::test::FloatingPointTypes);

TYPED_TEST(MD5HashTestFloatTyped, TestExtremes)
{
  using T = TypeParam;
  T min   = std::numeric_limits<T>::min();
  T max   = std::numeric_limits<T>::max();
  T nan   = std::numeric_limits<T>::quiet_NaN();
  T inf   = std::numeric_limits<T>::infinity();

  fixed_width_column_wrapper<T> const col1({T(0.0), T(100.0), T(-100.0), min, max, nan, inf, -inf});
  fixed_width_column_wrapper<T> const col2(
    {T(-0.0), T(100.0), T(-100.0), min, max, -nan, inf, -inf});

  auto const input1 = cudf::table_view({col1});
  auto const input2 = cudf::table_view({col2});

  auto const output1 = cudf::hash(input1, cudf::hash_id::HASH_MD5);
  auto const output2 = cudf::hash(input2, cudf::hash_id::HASH_MD5);

  CUDF_TEST_EXPECT_COLUMNS_EQUAL(output1->view(), output2->view(), verbosity);
}

TYPED_TEST(MD5HashTestFloatTyped, TestListExtremes)
{
  using T = TypeParam;
  T min   = std::numeric_limits<T>::min();
  T max   = std::numeric_limits<T>::max();
  T nan   = std::numeric_limits<T>::quiet_NaN();
  T inf   = std::numeric_limits<T>::infinity();

  lists_column_wrapper<T> const col1(
    {{T(0.0)}, {T(100.0), T(-100.0)}, {min, max, nan}, {inf, -inf}});
  lists_column_wrapper<T> const col2(
    {{T(-0.0)}, {T(100.0), T(-100.0)}, {min, max, -nan}, {inf, -inf}});

  auto const input1 = cudf::table_view({col1});
  auto const input2 = cudf::table_view({col2});

  auto const output1 = cudf::hash(input1, cudf::hash_id::HASH_MD5);
  auto const output2 = cudf::hash(input2, cudf::hash_id::HASH_MD5);

  CUDF_TEST_EXPECT_COLUMNS_EQUAL(output1->view(), output2->view(), verbosity);
}

CUDF_TEST_PROGRAM_MAIN()
