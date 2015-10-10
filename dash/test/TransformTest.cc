#include <libdash.h>
#include <gtest/gtest.h>
#include "TestBase.h"
#include "TransformTest.h"

#include <array>

TEST_F(TransformTest, ArrayGlobalPlusLocalBlocking)
{
  // Add local range to every block in global range
  const size_t num_elem_local = 5;
  size_t num_elem_total = _dash_size * num_elem_local;
  dash::Array<int> array_dest(num_elem_total, dash::BLOCKED);
  std::array<int, num_elem_local> local;

  // Initialize result array: [ 100, 100, ... | 200, 200, ... ]
  for (auto l_it = array_dest.lbegin(); l_it != array_dest.lend(); ++l_it) {
    *l_it = (dash::myid() + 1) * 100;
  }

  // Every unit adds a local range of elements to every block in a global
  // array.

  // Initialize local values, e.g. unit 2: [ 2000, 2001, 2002, ... ]
  for (auto l_idx = 0; l_idx < num_elem_local; ++l_idx) {
    local[l_idx] = ((dash::myid() + 1) * 1000) + (l_idx + 1);
  }

  // Accumulate local range to every block in the array:
  for (auto block_idx = 0; block_idx < _dash_size; ++block_idx) {
    auto block_offset = block_idx * num_elem_local;
    dash::transform<int>(&(*local.begin()), &(*local.end()), // A
                         array_dest.begin() + block_offset,  // B
                         array_dest.begin() + block_offset,  // B = op(B, A)
                         dash::plus<int>());                 // op
  }

  dash::barrier();
  
  // Verify values in local partition of array:
  
  // Gaussian sum of all local values accumulated = 1100 + 1200 + ...
  int global_acc = ((dash::myid() + 1) * 100) +
                   ((_dash_size + 1) * _dash_size * 1000) / 2;
  for (auto l_idx = 0; l_idx < num_elem_local; ++l_idx) {
    int expected = global_acc + ((l_idx + 1) * _dash_size);
    LOG_MESSAGE("array_dest.local[%d]: %p", l_idx, &array_dest.local[l_idx]);
    ASSERT_EQ_U(expected, array_dest.local[l_idx]);
  }
}

TEST_F(TransformTest, ArrayGlobalPlusGlobalBlocking)
{
  // Add values in global range to values in other global range
  const size_t num_elem_local = 100;
  size_t num_elem_total = _dash_size * num_elem_local;
  dash::Array<int> array_dest(num_elem_total, dash::BLOCKED);
  dash::Array<int> array_values(num_elem_total, dash::BLOCKED);

  // Initialize result array: [ 100, 100, ... | 200, 200, ... ]
  for (auto l_it = array_dest.lbegin(); l_it != array_dest.lend(); ++l_it) {
    *l_it = (dash::myid() + 1) * 100;
  }

  // Every unit adds a local range of elements to every block in a global
  // array.

  // Initialize values to add, e.g. unit 2: [ 2000, 2001, 2002, ... ]
  for (auto l_idx = 0; l_idx < num_elem_local; ++l_idx) {
    array_values.local[l_idx] = ((dash::myid() + 1) * 1000) + (l_idx + 1);
  }

  // Accumulate local range to every block in the array:
  dash::transform<int>(array_values.begin(), array_values.end(), // A
                       array_dest.begin(),                       // B
                       array_dest.begin(),                       // B = B op A
                       dash::plus<int>());                       // op

  dash::barrier();
  
  // Verify values in local partition of array:
  
  for (auto l_idx = 0; l_idx < num_elem_local; ++l_idx) {
    int expected = (dash::myid() + 1) * 100 +               // initial value
                   (dash::myid() + 1) * 1000 + (l_idx + 1); // added value
    ASSERT_EQ_U(expected, array_dest.local[l_idx]);
  }
}

TEST_F(TransformTest, MatrixGlobalPlusGlobalBlocking)
{
  LOG_MESSAGE("START");
  // Block-wise addition (a += b) of two matrices
  typedef typename dash::Matrix<int, 2>::index_type index_t;
  dart_unit_t myid   = dash::myid();
  size_t num_units   = dash::Team::All().size();
  size_t tilesize_x  = 7;
  size_t tilesize_y  = 3;
  size_t tilesize    = tilesize_x * tilesize_y;
  size_t extent_cols = tilesize_x * num_units * 2;
  size_t extent_rows = tilesize_y * num_units * 2;
  dash::Matrix<int, 2> matrix_a(
                         dash::SizeSpec<2>(
                           extent_cols,
                           extent_rows),
                         dash::DistributionSpec<2>(
                           dash::TILE(tilesize_x),
                           dash::TILE(tilesize_y)));
  dash::Matrix<int, 2> matrix_b(
                         dash::SizeSpec<2>(
                           extent_cols,
                           extent_rows),
                         dash::DistributionSpec<2>(
                           dash::TILE(tilesize_x),
                           dash::TILE(tilesize_y)));
  size_t matrix_size = extent_cols * extent_rows;
  ASSERT_EQ(matrix_size, matrix_a.size());
  ASSERT_EQ(extent_cols, matrix_a.extent(0));
  ASSERT_EQ(extent_rows, matrix_a.extent(1));
  LOG_MESSAGE("Matrix size: %d", matrix_size);

  // Fill matrix
  if(myid == 0) {
    LOG_MESSAGE("Assigning matrix values");
    for(int i = 0; i < matrix_a.extent(0); ++i) {
      for(int k = 0; k < matrix_a.extent(1); ++k) {
        auto value = (i * 1000) + (k * 1);
        LOG_MESSAGE("Setting matrix[%d][%d] = %d",
                    i, k, value);
        matrix_a[i][k] = value * 100000;
        matrix_b[i][k] = value;
      }
    }
  }
  LOG_MESSAGE("Wait for team barrier ...");
  dash::Team::All().barrier();
  LOG_MESSAGE("Team barrier passed");

  // Offset and extents of first block in global cartesian space:
  auto first_g_block_a = matrix_a.pattern().block(0);
  // Global coordinates of first element in first global block:
  std::array<index_t, 2> first_g_block_a_begin   = { 0, 0 };
  std::array<index_t, 2> first_g_block_a_offsets = first_g_block_a.offsets();
  ASSERT_EQ_U(first_g_block_a_begin,
              first_g_block_a.offsets());

  // Offset and extents of first block in local cartesian space:
  auto first_l_block_a = matrix_a.pattern().local_block(0);
  // Global coordinates of first element in first local block:
  std::array<index_t, 2> first_l_block_a_begin   = {
                           static_cast<index_t>(myid * tilesize_x), 0 };
  std::array<index_t, 2> first_l_block_a_offsets = first_l_block_a.offsets();
  ASSERT_EQ_U(first_l_block_a_begin,
              first_l_block_a_offsets);
}