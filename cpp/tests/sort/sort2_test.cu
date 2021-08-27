#include <cudf_test/base_fixture.hpp>
#include <cudf_test/column_utilities.hpp>
#include <cudf_test/column_wrapper.hpp>
#include <cudf_test/table_utilities.hpp>
#include <cudf_test/type_lists.hpp>

#include <cudf/column/column_factories.hpp>
#include <cudf/copying.hpp>
#include <cudf/fixed_point/fixed_point.hpp>
#include <cudf/sorting.hpp>
#include <cudf/table/table_view.hpp>
#include <cudf/types.hpp>
#include <cudf/utilities/type_dispatcher.hpp>

#include <vector>

struct NewRowOp : public cudf::test::BaseFixture {
};

#include <cudf/sort2.cuh>

TEST_F(NewRowOp, BasicTest)
{
  using Type           = int;
  using column_wrapper = cudf::test::fixed_width_column_wrapper<Type>;
  std::default_random_engine generator;
  std::uniform_int_distribution<int> distribution(0, 100);

  const cudf::size_type n_rows{1 << 6};
  const cudf::size_type n_cols{1};
  const cudf::size_type depth{8};

  // Create columns with values in the range [0,100)
  std::vector<column_wrapper> columns;
  columns.reserve(n_cols);
  std::generate_n(std::back_inserter(columns), n_cols, [&, n_rows]() {
    auto elements = cudf::detail::make_counting_transform_iterator(
      0, [&](auto row) { return distribution(generator); });
    auto valids = cudf::detail::make_counting_transform_iterator(
      0, [](auto i) { return i % 3 == 0 ? false : true; });
    return column_wrapper(elements, elements + n_rows, valids);
  });

  std::vector<std::unique_ptr<cudf::column>> cols;
  std::transform(columns.begin(), columns.end(), std::back_inserter(cols), [](column_wrapper& col) {
    return col.release();
  });

  std::vector<std::unique_ptr<cudf::column>> child_cols = std::move(cols);
  // Lets add some layers
  for (int i = 0; i < depth; i++) {
    std::vector<bool> struct_validity;
    std::uniform_int_distribution<int> bool_distribution(0, 10 * (i + 1));
    std::generate_n(
      std::back_inserter(struct_validity), n_rows, [&]() { return bool_distribution(generator); });
    cudf::test::structs_column_wrapper struct_col(std::move(child_cols), struct_validity);
    child_cols = std::vector<std::unique_ptr<cudf::column>>{};
    child_cols.push_back(struct_col.release());
  }

  cudf::test::print(child_cols[0]->view());

  // // Create table view
  // auto input = cudf::table_view({struct_col});
  auto input = cudf::table(std::move(child_cols));

  auto result1 = cudf::sorted_order(input);
  // cudf::test::print(result1->view());
  auto result2 = cudf::detail::sorted_order2(input);
  // cudf::test::print(result2->view());
  cudf::test::expect_columns_equal(result1->view(), result2->view());
}
