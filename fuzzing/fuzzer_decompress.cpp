#include <cstddef>
#include <cstdint>

#include "lzav.h"

/*
 * This takes a buffer supposedly containing compressed data
 * and decompresses it.
 */
extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{

  const int comp_len = 20000;
  char decomp_buf[comp_len];

  const int l = lzav_decompress(Data, decomp_buf, Size, comp_len);

  return 0;
}
