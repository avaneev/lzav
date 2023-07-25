#include <cassert>
#include <cstddef>
#include <cstdint>

#include "lzav.h"

/*
 * This takes a buffer with arbrirtrary data and compresses it.
 */
extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{

  const int comp_len_max = 20000;
  char comp_buf[comp_len_max];

  const int max_len = lzav_compress_bound(Size);
  if (max_len <= comp_len_max) {
    int comp_len = lzav_compress_default(Data, comp_buf, Size, comp_len_max);
    assert(comp_len >= 0);
  }

  return 0;
}
