#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "lzav.h"

/*
 * This takes a buffer with arbitrary data and compresses it,
 * decompresses it and makes sure it is identical to the original.
 */
extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{

  const int comp_len_max = 20000;
  char comp_buf[comp_len_max];

  const int max_len = lzav_compress_bound(Size);
  if (max_len > comp_len_max) {
    return 0;
  }
  const int comp_len =
    lzav_compress_default(Data, comp_buf, Size, comp_len_max);
  assert(comp_len >= 0);

  // decompress it
  const int decomp_len = 20000;
  char decomp_buf[decomp_len];

  const int l = lzav_decompress(comp_buf, decomp_buf, comp_len, decomp_len);

  assert(0 == std::memcmp(Data, decomp_buf, Size));

  return 0;
}
