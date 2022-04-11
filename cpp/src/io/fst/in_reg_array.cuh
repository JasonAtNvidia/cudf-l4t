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
#pragma once

#include <cub/cub.cuh>

#include <cstdint>

namespace cudf {
namespace io {
namespace fst {
namespace detail {

/**
 * @brief A bit-packed array of items that can be backed by registers yet allows to be dynamically
 * addressed at runtime. The data struture is explained in greater detail in the paper <a
 * href="http://www.vldb.org/pvldb/vol13/p616-stehle.pdf">ParPaRaw: Massively Parallel Parsing of
 * Delimiter-Separated Raw Data</a>.
 *
 * @tparam NUM_ITEMS The maximum number of items this data structure is supposed to store
 * @tparam MAX_ITEM_VALUE The maximum value that one item can represent
 * @tparam BackingFragmentT The data type that is holding the fragments
 */
template <uint32_t NUM_ITEMS, uint32_t MAX_ITEM_VALUE, typename BackingFragmentT = uint32_t>
struct MultiFragmentInRegArray {
  /// [b] Minimum number of bits required to represent all values from [0, MAX_ITEM_VALUE]
  static constexpr uint32_t MIN_BITS_PER_ITEM =
    (MAX_ITEM_VALUE == 0) ? 1 : cub::Log2<(MAX_ITEM_VALUE + 1)>::VALUE;

  /// Number of bits that each fragment can store
  static constexpr uint32_t NUM_BITS_PER_FRAGMENT = sizeof(BackingFragmentT) * 8;

  /// [a] The number of bits per fragment per item in the array
  static constexpr uint32_t AVAIL_BITS_PER_FRAG_ITEM = NUM_BITS_PER_FRAGMENT / NUM_ITEMS;

  /// [k] The number of bits per item per fragment to be a power of two to avoid costly integer
  /// multiplication
  /// TODO: specialise for VOLTA and later architectures that have efficient integer multiplication
  static constexpr uint32_t BITS_PER_FRAG_ITEM =
    0x01U << (cub::Log2<(AVAIL_BITS_PER_FRAG_ITEM + 1)>::VALUE - 1);
  static constexpr uint32_t LOG2_BITS_PER_FRAG_ITEM = cub::Log2<BITS_PER_FRAG_ITEM>::VALUE;

  // [f] Number of fragments required to store and to reconstruct an item
  static constexpr uint32_t FRAGMENTS_PER_ITEM =
    (MIN_BITS_PER_ITEM + BITS_PER_FRAG_ITEM - 1) / BITS_PER_FRAG_ITEM;

  //------------------------------------------------------------------------------
  // MEMBER VARIABLES
  //------------------------------------------------------------------------------
  __device__ __host__ __forceinline__ unsigned int bfe(const unsigned int& data,
                                                       unsigned int bit_start,
                                                       unsigned int num_bits) const
  {
#if CUB_PTX_ARCH > 0
    return cub::BFE(data, bit_start, num_bits);
#else
    const unsigned int MASK = (1 << num_bits) - 1;
    return (data >> bit_start) & MASK;
#endif
  }

  __device__ __host__ __forceinline__ void bfi(unsigned int& data,
                                               unsigned int bits,
                                               unsigned int bit_start,
                                               unsigned int num_bits) const
  {
#if CUB_PTX_ARCH > 0
    cub::BFI(data, data, bits, bit_start, num_bits);
#else
    unsigned int x      = bits << bit_start;
    unsigned int y      = data;
    unsigned int MASK_X = ((1 << num_bits) - 1) << bit_start;
    unsigned int MASK_Y = ~MASK_X;
    data                = (y & MASK_Y) | (x & MASK_X);
#endif
  }

  BackingFragmentT data[FRAGMENTS_PER_ITEM];

  //------------------------------------------------------------------------------
  // ACCESSORS
  //------------------------------------------------------------------------------
  __host__ __device__ __forceinline__ uint32_t Get(int32_t index) const
  {
    uint32_t val = 0;

    // #pragma unroll
    for (uint32_t i = 0; i < FRAGMENTS_PER_ITEM; ++i) {
      val = val | bfe(data[i], index * BITS_PER_FRAG_ITEM, BITS_PER_FRAG_ITEM)
                    << (i * BITS_PER_FRAG_ITEM);
    }
    return val;
  }

  __host__ __device__ __forceinline__ void Set(uint32_t index, uint32_t value)
  {
    // #pragma unroll
    for (uint32_t i = 0; i < FRAGMENTS_PER_ITEM; ++i) {
      uint32_t frag_bits = bfe(value, i * BITS_PER_FRAG_ITEM, BITS_PER_FRAG_ITEM);
      bfi(data[i], frag_bits, index * BITS_PER_FRAG_ITEM, BITS_PER_FRAG_ITEM);
    }
  }

  //------------------------------------------------------------------------------
  // CONSTRUCTORS
  //------------------------------------------------------------------------------
  __host__ __device__ __forceinline__ MultiFragmentInRegArray()
  {
    for (uint32_t i = 0; i < FRAGMENTS_PER_ITEM; ++i) {
      data[i] = 0;
    }
  }

  __host__ __device__ __forceinline__ MultiFragmentInRegArray(uint32_t const (&array)[NUM_ITEMS])
  {
    for (uint32_t i = 0; i < NUM_ITEMS; ++i) {
      Set(i, array[i]);
    }
  }
};

}  // namespace detail
}  // namespace fst
}  // namespace io
}  // namespace cudf
